#include <ledger/ContractHelper.h>
#include <ledger/KeyValueHelper.h>
#include "transactions/review_request/ReviewRequestHelper.h"
#include "transactions/ManageContractOpFrame.h"
#include "ledger/TrustFrame.h"
#include "ledger/TrustHelper.h"
#include "ledger/BalanceHelper.h"
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
    std::vector<AccountType> allowedAccountTypes = {AccountType::MASTER, AccountType::GENERAL,
                                                    AccountType::NOT_VERIFIED,
                               AccountType::SYNDICATE, AccountType::EXCHANGE, AccountType::VERIFIED,
                               AccountType::ACCREDITED_INVESTOR, AccountType::INSTITUTIONAL_INVESTOR};

    return SourceDetails(allowedAccountTypes,
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
ManageContractOpFrame::checkIsAllowed(ContractFrame::pointer contractFrame)
{
    bool isAllowed = contractFrame->getContractor() == getSourceID();
    isAllowed = isAllowed || (contractFrame->getCustomer() == getSourceID());

    if (contractFrame->getState() & static_cast<uint32_t>(ContractState::DISPUTING))
        isAllowed = isAllowed || (contractFrame->getEscrow() == getSourceID());

    return isAllowed;
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

    if (!checkIsAllowed(contractFrame))
    {
        innerResult().code(ManageContractResultCode::NOT_ALLOWED);
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
    innerResult().response().data.action(ManageContractAction::ADD_DETAILS);

    auto maxContractDetails = obtainMaxContractDetailsCount(app, db, delta);

    auto actualDetailsCount = contractFrame->getContractDetailsCount();

    if (actualDetailsCount >= maxContractDetails)
    {
        innerResult().code(ManageContractResultCode::TOO_MANY_CONTRACT_DETAILS);
        return false;
    }

    auto maxContractDetailLength = obtainMaxContractDetailLength(app, db, delta);

    if (mManageContract.data.details().size() > maxContractDetailLength)
    {
        innerResult().code(ManageContractResultCode::DETAILS_TOO_LONG);
        return false;
    }

    contractFrame->addContractDetails(mManageContract.data.details());
    ContractHelper::Instance()->storeChange(delta, db, contractFrame->mEntry);

    return true;
}

uint64_t
ManageContractOpFrame::obtainMaxContractDetailsCount(Application& app, Database& db, LedgerDelta& delta)
{
    auto maxContractDetailsKey = ManageKeyValueOpFrame::makeMaxContractDetailsKey();
    auto maxContractDetailsKeyValue = KeyValueHelper::Instance()->loadKeyValue(maxContractDetailsKey, db, &delta);

    if (!maxContractDetailsKeyValue)
    {
        return app.getMaxContractDetailsCount();
    }

    if (maxContractDetailsKeyValue->getKeyValueEntryType() != KeyValueEntryType::UINT32)
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected database state. "
             << "Expected max contract details key value to be UINT32. Actual: "
             << xdr::xdr_traits<KeyValueEntryType>::enum_name(maxContractDetailsKeyValue->getKeyValueEntryType());
        throw std::runtime_error("Unexpected database state, expected max contract details key value to be UINT32");
    }

    return maxContractDetailsKeyValue->getKeyValue().value.ui32Value();
}

uint64_t
ManageContractOpFrame::obtainMaxContractDetailLength(Application& app, Database& db, LedgerDelta& delta)
{
    auto maxContractDetailLengthKey = ManageKeyValueOpFrame::makeMaxContractDetailLengthKey();
    auto maxContractDetailLengthKetValue = KeyValueHelper::Instance()->
            loadKeyValue(maxContractDetailLengthKey, db, &delta);

    if (!maxContractDetailLengthKetValue)
    {
        return app.getMaxContractDetailLength();
    }

    if (maxContractDetailLengthKetValue->getKeyValueEntryType() != KeyValueEntryType::UINT32)
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected database state. "
             << "Expected max contract detail length key value to be UINT32. Actual: "
             << xdr::xdr_traits<KeyValueEntryType>::enum_name(maxContractDetailLengthKetValue->getKeyValueEntryType());
        throw std::runtime_error("Unexpected database state, expected max contract detail length key value to be UINT32");
    }

    return maxContractDetailLengthKetValue->getKeyValue().value.ui32Value();
}

bool
ManageContractOpFrame::tryConfirmCompleted(ContractFrame::pointer contractFrame, Database &db, LedgerDelta &delta)
{
    innerResult().response().data.action(ManageContractAction::CONFIRM_COMPLETED);

    if (contractFrame->getEscrow() == getSourceID())
    {
        innerResult().code(ManageContractResultCode::CONFIRM_NOT_ALLOWED);
        return false;
    }

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

    return checkIsCompleted(contractFrame, invoiceRequests, db, delta);
}

bool
ManageContractOpFrame::checkIsCompleted(ContractFrame::pointer contractFrame,
                                        std::vector<ReviewableRequestFrame::pointer> invoiceRequests,
                                        Database& db, LedgerDelta& delta)
{
    if (!contractFrame->isBothConfirmed())
    {
        EntryHelperProvider::storeChangeEntry(delta, db, contractFrame->mEntry);
        innerResult().response().data.isCompleted() = false;
        return true;
    }

    innerResult().response().data.isCompleted() = true;

    auto requestHelper = ReviewableRequestHelper::Instance();

    for (ReviewableRequestFrame::pointer invoiceRequest : invoiceRequests)
    {
        auto invoice = invoiceRequest->getRequestEntry().body.invoiceRequest();
        auto balanceHelper = BalanceHelper::Instance();
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

    if (contractFrame->getState() &
        static_cast<uint32_t>(ContractState::DISPUTING))
    {
        innerResult().code(ManageContractResultCode::DISPUTE_ALREADY_STARTED);
        return false;
    }

    auto maxDisputeLength = obtainMaxContractDetailLength(app, db, delta);
    if (mManageContract.data.disputeReason().size() > maxDisputeLength)
    {
        innerResult().code(ManageContractResultCode::DISPUTE_REASON_TOO_LONG);
        return false;
    }

    contractFrame->startDispute(getSourceID(), mManageContract.data.disputeReason());
    ContractHelper::Instance()->storeChange(delta, db, contractFrame->mEntry);

    return true;
}

bool
ManageContractOpFrame::tryResolveDispute(ContractFrame::pointer contractFrame,
                                         Database &db, LedgerDelta &delta)
{
    innerResult().response().data.action(ManageContractAction::RESOLVE_DISPUTE);

    if (!(contractFrame->getEscrow() == getSourceID()))
    {
        innerResult().code(ManageContractResultCode::RESOLVE_DISPUTE_NOW_ALLOWED);
        return false;
    }

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
    auto balanceHelper = BalanceHelper::Instance();
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
    auto balanceHelper = BalanceHelper::Instance();
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
