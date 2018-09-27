#include <ledger/ContractHelper.h>
#include <ledger/KeyValueHelperLegacy.h>
#include "transactions/review_request/ReviewRequestHelper.h"
#include "transactions/ManageContractOpFrame.h"
#include "ledger/TrustFrame.h"
#include "ledger/TrustHelper.h"
#include "ledger/BalanceHelperLegacy.h"
#include "ledger/ReviewableRequestHelper.h"
#include "crypto/SHA.h"
#include "database/Database.h"
#include "main/Application.h"
#include "medida/meter.h"
#include "medida/metrics_registry.h"
#include "ManageKeyValueOpFrame.h"

namespace stellar
{
using xdr::operator==;

std::unordered_map<AccountID, CounterpartyDetails>
ManageContractOpFrame::getCounterpartyDetails(Database & db, LedgerDelta * delta) const
{
    // no counterparties
    return{};
}

SourceDetails
ManageContractOpFrame::getSourceAccountDetails(
        std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
        int32_t ledgerVersion) const
{
    return SourceDetails(getAllAccountTypes(),
                         mSourceAccount->getHighThreshold(),
                         static_cast<int32_t>(SignerType::CONTRACT_MANAGER));
}

ManageContractOpFrame::ManageContractOpFrame(Operation const& op, OperationResult& res,
        TransactionFrame& parentTx)
        : OperationFrame(op, res, parentTx)
        , mManageContract(mOperation.body.manageContractOp())
{
}

std::string
ManageContractOpFrame::getInnerResultCodeAsStr()
{
    const auto result = getResult();
    const auto code = getInnerCode(result);
    return xdr::xdr_traits<ManageContractResultCode>::enum_name(code);
}

bool
ManageContractOpFrame::ensureIsAllowed(std::vector<AccountID> validSources)
{
    auto source = getSourceID();
    for (AccountID validSource : validSources)
    {
        if (validSource == source)
            return true;
    }

    innerResult().code(ManageContractResultCode::NOT_ALLOWED);
    return false;
}

bool
ManageContractOpFrame::doApply(Application& app, LedgerDelta& delta,
                                LedgerManager& ledgerManager)
{
    Database& db = ledgerManager.getDatabase();
    AccountEntry& account = mSourceAccount->getAccount();

    innerResult().code(ManageContractResultCode::SUCCESS);

    auto contractFrame = ContractHelper::Instance()->loadContract(mManageContract.contractID, db, &delta);

    if (!contractFrame)
    {
        innerResult().code(ManageContractResultCode::NOT_FOUND);
        return false;
    }

    switch (mManageContract.data.action())
    {
        case ManageContractAction::ADD_DETAILS:
            return tryAddContractDetails(contractFrame, app, db, delta);
        case ManageContractAction::START_DISPUTE:
            return tryStartDispute(contractFrame, app, db, delta);
        case ManageContractAction::CONFIRM_COMPLETED:
            return tryConfirmCompleted(contractFrame, db, delta);
        case ManageContractAction::RESOLVE_DISPUTE:
            return tryResolveDispute(contractFrame, db, delta);
        default:
            CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected source account. "
                                                   << "Expected contractor, customer or master";
            throw std::runtime_error("Unexpected source account. Expected contractor, customer or master");
    }
}

bool
ManageContractOpFrame::tryAddContractDetails(ContractFrame::pointer contractFrame,
                                             Application &app, Database &db, LedgerDelta &delta)
{
    std::vector<AccountID> validSources = {contractFrame->getContractor(), contractFrame->getCustomer()};
    if (contractFrame->isInState(ContractState::DISPUTING))
        validSources.emplace_back(contractFrame->getEscrow());

    if (!ensureIsAllowed(validSources)) {
        return false;
    }

    innerResult().response().data.action(ManageContractAction::ADD_DETAILS);

    auto maxContractDetailLength = obtainMaxContractDetailLength(app, db, delta);

    if (mManageContract.data.details().size() > maxContractDetailLength)
    {
        innerResult().code(ManageContractResultCode::DETAILS_TOO_LONG);
        return false;
    }

    return true;
}

uint64_t
ManageContractOpFrame::obtainMaxContractDetailLength(Application& app, Database& db, LedgerDelta& delta)
{
    auto maxContractDetailLengthKey = ManageKeyValueOpFrame::makeMaxContractDetailLengthKey();
    auto maxContractDetailLengthKeyValue = KeyValueHelperLegacy::Instance()->
            loadKeyValue(maxContractDetailLengthKey, db, &delta);

    if (!maxContractDetailLengthKeyValue)
    {
        return app.getMaxContractDetailLength();
    }

    if (maxContractDetailLengthKeyValue->getKeyValueEntryType() != KeyValueEntryType::UINT32)
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected database state. "
             << "Expected max contract detail length key value to be UINT32. Actual: "
             << xdr::xdr_traits<KeyValueEntryType>::enum_name(maxContractDetailLengthKeyValue->getKeyValueEntryType());
        throw std::runtime_error("Unexpected database state, expected max contract detail length key value to be UINT32");
    }

    return maxContractDetailLengthKeyValue->getKeyValue().value.ui32Value();
}

bool
ManageContractOpFrame::tryConfirmCompleted(ContractFrame::pointer contractFrame, Database &db, LedgerDelta &delta)
{
    if (!ensureIsAllowed({contractFrame->getContractor(), contractFrame->getCustomer()}))
        return false;

    innerResult().response().data.action(ManageContractAction::CONFIRM_COMPLETED);

    auto invoiceRequests = ReviewableRequestHelper::Instance()->loadRequests(
            contractFrame->getInvoiceRequestIDs(), db);

    if (!checkIsInvoicesApproved(invoiceRequests))
        return false;

    auto stateToBeAdded = ContractState::CONTRACTOR_CONFIRMED;

    if (contractFrame->getCustomer() == getSourceID())
    {
        stateToBeAdded = ContractState::CUSTOMER_CONFIRMED;
    }

    if (!contractFrame->addState(stateToBeAdded))
    {
        innerResult().code(ManageContractResultCode::ALREADY_CONFIRMED);
        return false;
    }

    EntryHelperProvider::storeChangeEntry(delta, db, contractFrame->mEntry);

    return tryCompleted(contractFrame, invoiceRequests, db, delta);
}

bool
ManageContractOpFrame::tryCompleted(ContractFrame::pointer contractFrame,
                                    std::vector<ReviewableRequestFrame::pointer> invoiceRequests,
                                    Database &db, LedgerDelta &delta)
{
    if (!contractFrame->isBothConfirmed())
    {
        innerResult().response().data.isCompleted() = false;
        return true;
    }

    innerResult().response().data.isCompleted() = true;

    auto requestHelper = ReviewableRequestHelper::Instance();

    for (ReviewableRequestFrame::pointer invoiceRequest : invoiceRequests)
    {
        auto invoice = invoiceRequest->getRequestEntry().body.invoiceRequest();
        auto balanceHelper = BalanceHelperLegacy::Instance();
        auto balanceFrame = balanceHelper->mustLoadBalance(invoice.receiverBalance, db, &delta);

        if (!balanceFrame->unlock(invoice.amount))
        {
            CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected balance state. "
                                                   << "Expected success unlock in manage contract. ";
            throw std::runtime_error("Unexpected balance state. Expected success unlock in manage contract.");
        }

        balanceHelper->storeChange(delta, db, balanceFrame->mEntry);
        requestHelper->storeDelete(delta, db, invoiceRequest->getKey());
    }

    EntryHelperProvider::storeDeleteEntry(delta, db, contractFrame->getKey());

    return true;
}

bool ManageContractOpFrame::checkIsInvoicesApproved(std::vector<ReviewableRequestFrame::pointer> invoiceRequests)
{
    for (ReviewableRequestFrame::pointer invoiceRequest : invoiceRequests)
    {
        auto invoice = invoiceRequest->getRequestEntry().body.invoiceRequest();
        if (!invoice.isApproved)
        {
            innerResult().code(ManageContractResultCode::INVOICE_NOT_APPROVED);
            return false;
        }
    }

    return true;
}

bool
ManageContractOpFrame::tryStartDispute(ContractFrame::pointer contractFrame,
                                       Application &app, Database &db, LedgerDelta &delta)
{
    innerResult().response().data.action(ManageContractAction::START_DISPUTE);

    if (contractFrame->isInState(ContractState::DISPUTING))
    {
        innerResult().code(ManageContractResultCode::DISPUTE_ALREADY_STARTED);
        return false;
    }

    if (!ensureIsAllowed({contractFrame->getContractor(), contractFrame->getCustomer()})) {
        return false;
    }

    auto maxDisputeLength = obtainMaxContractDetailLength(app, db, delta);
    if (mManageContract.data.disputeReason().size() > maxDisputeLength)
    {
        innerResult().code(ManageContractResultCode::DISPUTE_REASON_TOO_LONG);
        return false;
    }

    contractFrame->addState(ContractState::DISPUTING);
    ContractHelper::Instance()->storeChange(delta, db, contractFrame->mEntry);

    return true;
}

bool
ManageContractOpFrame::tryResolveDispute(ContractFrame::pointer contractFrame,
                                         Database &db, LedgerDelta &delta)
{
    if (!ensureIsAllowed({contractFrame->getEscrow()}))
        return false;

    innerResult().response().data.action(ManageContractAction::RESOLVE_DISPUTE);

    EntryHelperProvider::storeDeleteEntry(delta, db, contractFrame->getKey());

    if (mManageContract.data.isRevert())
    {
        return revertInvoicesAmounts(contractFrame, db, delta);
    }

    unlockApprovedInvoicesAmounts(contractFrame, db, delta);

    return true;
}

bool
ManageContractOpFrame::revertInvoicesAmounts(ContractFrame::pointer contractFrame,
                                             Database& db, LedgerDelta& delta)
{
    auto balanceHelper = BalanceHelperLegacy::Instance();
    auto requestHelper = ReviewableRequestHelper::Instance();
    auto invoiceRequests = requestHelper->loadRequests(contractFrame->getInvoiceRequestIDs(), db);

    for (ReviewableRequestFrame::pointer invoiceRequest : invoiceRequests)
    {
        requestHelper->storeDelete(delta, db, invoiceRequest->getKey());

        auto invoice = invoiceRequest->getRequestEntry().body.invoiceRequest();
        if (!invoice.isApproved)
        {
            continue;
        }

        auto contractorBalance = balanceHelper->mustLoadBalance(invoice.receiverBalance, db, &delta);
        if (!contractorBalance->tryChargeFromLocked(invoice.amount))
        {
            CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected balance state. "
                                                   << "Expected success charge from locked in manage contract. ";
            throw std::runtime_error("Unexpected balance state. Expected success charge from locked in manage contract.");
        }
        balanceHelper->storeChange(delta, db, contractorBalance->mEntry);

        auto customerBalance = balanceHelper->mustLoadBalance(invoice.senderBalance, db, &delta);
        if (!customerBalance->tryFundAccount(invoice.amount))
        {
            innerResult().code(ManageContractResultCode::CUSTOMER_BALANCE_OVERFLOW);
            return false;
        }
        balanceHelper->storeChange(delta, db, customerBalance->mEntry);
    }

    return true;
}

void
ManageContractOpFrame::unlockApprovedInvoicesAmounts(ContractFrame::pointer contractFrame,
                                                     Database& db, LedgerDelta & delta)
{
    auto balanceHelper = BalanceHelperLegacy::Instance();
    auto requestHelper = ReviewableRequestHelper::Instance();
    auto invoiceRequests = requestHelper->loadRequests(contractFrame->getInvoiceRequestIDs(), db);

    for (ReviewableRequestFrame::pointer invoiceRequest : invoiceRequests)
    {
        requestHelper->storeDelete(delta, db, invoiceRequest->getKey());

        auto invoice = invoiceRequest->getRequestEntry().body.invoiceRequest();
        if (!invoice.isApproved)
        {
            continue;
        }

        auto contractorBalance = balanceHelper->mustLoadBalance(invoice.receiverBalance, db, &delta);
        if (!contractorBalance->unlock(invoice.amount))
        {
            CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected balance state. "
                                                   << "Expected success charge from locked in manage contract. ";
            throw std::runtime_error("Unexpected balance state. Expected success charge from locked in manage contract.");
        }
        balanceHelper->storeChange(delta, db, contractorBalance->mEntry);
    }
}

bool
ManageContractOpFrame::doCheckValid(Application& app)
{
    if (((mManageContract.data.action() == ManageContractAction::ADD_DETAILS) &&
         mManageContract.data.details().empty()) ||
        ((mManageContract.data.action() == ManageContractAction::START_DISPUTE) &&
         mManageContract.data.disputeReason().empty()))
    {
        innerResult().code(ManageContractResultCode::MALFORMED);
        return false;
    }

    return true;
}

}
