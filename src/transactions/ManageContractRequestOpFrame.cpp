#include <ledger/ReviewableRequestFrame.h>
#include "transactions/ManageContractRequestOpFrame.h"
#include "database/Database.h"
#include "main/Application.h"
#include "medida/meter.h"
#include <crypto/SHA.h>
#include <ledger/ReviewableRequestHelper.h>
#include <ledger/KeyValueHelperLegacy.h>
#include <ledger/ContractHelper.h>
#include "medida/metrics_registry.h"
#include "ledger/LedgerDelta.h"
#include "ledger/BalanceHelperLegacy.h"
#include "ManageContractOpFrame.h"
#include "ManageKeyValueOpFrame.h"

namespace stellar
{
using xdr::operator==;

std::unordered_map<AccountID, CounterpartyDetails>
ManageContractRequestOpFrame::getCounterpartyDetails(Database & db, LedgerDelta * delta) const
{
    if (mManageContractRequest.details.action() == ManageContractRequestAction::REMOVE) {
        // no counterparties
        return{};
    }
    return{
        {mManageContractRequest.details.contractRequest().customer,
                CounterpartyDetails(getAllAccountTypes(), true, true)},
        {mManageContractRequest.details.contractRequest().escrow,
                CounterpartyDetails(getAllAccountTypes(), true, true)},
    };
}

SourceDetails
ManageContractRequestOpFrame::getSourceAccountDetails(
        std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
        int32_t ledgerVersion) const
{
    return SourceDetails(getAllAccountTypes(), mSourceAccount->getHighThreshold(),
                         static_cast<int32_t>(SignerType::CONTRACT_MANAGER));
}

ManageContractRequestOpFrame::ManageContractRequestOpFrame(Operation const& op, OperationResult& res,
        TransactionFrame& parentTx) : OperationFrame(op, res, parentTx),
        mManageContractRequest(mOperation.body.manageContractRequestOp())
{
}

bool
ManageContractRequestOpFrame::doApply(Application& app, LedgerDelta& delta, LedgerManager& ledgerManager)
{
    Database& db = ledgerManager.getDatabase();
    innerResult().code(ManageContractRequestResultCode::SUCCESS);

    if (mManageContractRequest.details.action() == ManageContractRequestAction::CREATE)
    {
        return createManageContractRequest(app, delta, ledgerManager);
    }

    auto reviewableRequestHelper = ReviewableRequestHelper::Instance();
    auto reviewableRequest = reviewableRequestHelper->loadRequest(mManageContractRequest.details.requestID(), db);

    if (ledgerManager.shouldUse(LedgerVersion::ADD_CUSTOMER_DETAILS_TO_CONTRACT))
    {
        if (!reviewableRequest || reviewableRequest->getRequestType() != ReviewableRequestType::CONTRACT)
        {
            innerResult().code(ManageContractRequestResultCode::NOT_FOUND);
            return false;
        }
    }
    else
    {
        if (!reviewableRequest || reviewableRequest->getRequestType() != ReviewableRequestType::INVOICE)
        {
            innerResult().code(ManageContractRequestResultCode::NOT_FOUND);
            return false;
        }
    }

    if (!(reviewableRequest->getRequestor() == getSourceID()))
    {
        innerResult().code(ManageContractRequestResultCode::NOT_ALLOWED_TO_REMOVE);
        return false;
    }

    LedgerKey requestKey;
    requestKey.type(LedgerEntryType::REVIEWABLE_REQUEST);
    requestKey.reviewableRequest().requestID = mManageContractRequest.details.requestID();
    reviewableRequestHelper->storeDelete(delta, db, requestKey);

    innerResult().success().details.action(ManageContractRequestAction::REMOVE);

    return true;
}

bool
ManageContractRequestOpFrame::createManageContractRequest(Application& app, LedgerDelta& delta,
                                                          LedgerManager& ledgerManager)
{
    Database& db = ledgerManager.getDatabase();
    auto& contractRequest = mManageContractRequest.details.contractRequest();

    if (!checkMaxContractsForContractor(app, db, delta))
        return false;

    if (!checkMaxContractDetailLength(app, db, delta))
        return false;

    ReviewableRequestEntry::_body_t body;
    body.type(ReviewableRequestType::CONTRACT);
    body.contractRequest() = contractRequest;

    auto request = ReviewableRequestFrame::createNewWithHash(delta, getSourceID(),
                                                             contractRequest.customer,
                                                             nullptr, body,
                                                             ledgerManager.getCloseTime());

    EntryHelperProvider::storeAddEntry(delta, db, request->mEntry);

    innerResult().success().details.action(ManageContractRequestAction::CREATE);
    innerResult().success().details.response().requestID = request->getRequestID();

    return true;
}

bool
ManageContractRequestOpFrame::checkMaxContractsForContractor(Application& app, Database& db, LedgerDelta& delta)
{
    auto maxContractsCount = obtainMaxContractsForContractor(app, db, delta);
    auto contractsCount = ContractHelper::Instance()->countContracts(getSourceID(), db);

    if (contractsCount >= maxContractsCount)
    {
        innerResult().code(ManageContractRequestResultCode::TOO_MANY_CONTRACTS);
        return false;
    }

    return true;
}
}

uint64_t
ManageContractRequestOpFrame::obtainMaxContractsForContractor(Application& app, Database& db, LedgerDelta& delta)
{
    auto maxContractsCountKey = ManageKeyValueOpFrame::makeMaxContractsCountKey();
    auto maxContractsCountKeyValue = KeyValueHelperLegacy::Instance()->
            loadKeyValue(maxContractsCountKey, db, &delta);

    if (!maxContractsCountKeyValue)
    {
        return app.getMaxContractsForContractor();
    }

    if (maxContractsCountKeyValue->getKeyValueEntryType() != KeyValueEntryType::UINT32)
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected database state. "
             << "Expected max contracts count key value to be UINT32. Actual: "
             << xdr::xdr_traits<KeyValueEntryType>::enum_name(maxContractsCountKeyValue->getKeyValueEntryType());
        throw std::runtime_error("Unexpected database state, expected max contracts count key value to be UINT32");
    }

    return maxContractsCountKeyValue->getKeyValue().value.ui32Value();
}

bool
ManageContractRequestOpFrame::checkMaxContractDetailLength(Application& app, Database& db, LedgerDelta& delta)
{
    auto maxContractInitialDetailLength = obtainMaxContractInitialDetailLength(app, db, delta);

    if (mManageContractRequest.details.contractRequest().details.size() > maxContractInitialDetailLength)
    {
        innerResult().code(ManageContractRequestResultCode::DETAILS_TOO_LONG);
        return false;
    }

    return true;
}

uint64_t
ManageContractRequestOpFrame::obtainMaxContractInitialDetailLength(Application& app, Database& db, LedgerDelta& delta)
{
    auto maxContractInitialDetailLengthKey = ManageKeyValueOpFrame::makeMaxContractInitialDetailLengthKey();
    auto maxContractInitialDetailLengthKeyValue = KeyValueHelperLegacy::Instance()->
            loadKeyValue(maxContractInitialDetailLengthKey, db, &delta);

    if (!maxContractInitialDetailLengthKeyValue)
    {
        return app.getMaxContractInitialDetailLength();
    }

    if (maxContractInitialDetailLengthKeyValue->getKeyValueEntryType() != KeyValueEntryType::UINT32)
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected database state. "
             << "Expected max contracts initial detail length key value to be UINT32. Actual: "
             << xdr::xdr_traits<KeyValueEntryType>::enum_name(maxContractInitialDetailLengthKeyValue->getKeyValueEntryType());
        throw std::runtime_error("Unexpected database state, expected max contracts initial detail length key value to be UINT32");
    }

    return maxContractInitialDetailLengthKeyValue->getKeyValue().value.ui32Value();
}

bool
ManageContractRequestOpFrame::doCheckValid(Application& app)
{
    if (mManageContractRequest.details.action() != ManageContractRequestAction::CREATE)
        return true;

    if (mManageContractRequest.details.contractRequest().details.empty())
    {
        innerResult().code(ManageContractRequestResultCode::MALFORMED);
        return false;
    }

    if (mManageContractRequest.details.contractRequest().customer == getSourceID())
    {
        innerResult().code(ManageContractRequestResultCode::MALFORMED);
        return false;
    }

    if (mManageContractRequest.details.contractRequest().escrow == getSourceID())
    {
        innerResult().code(ManageContractRequestResultCode::MALFORMED);
        return false;
    }

    if (mManageContractRequest.details.contractRequest().customer ==
        mManageContractRequest.details.contractRequest().escrow)
    {
        innerResult().code(ManageContractRequestResultCode::MALFORMED);
        return false;
    }

    if (app.getLedgerManager().getCloseTime() >=
        mManageContractRequest.details.contractRequest().endTime)
    {
        innerResult().code(ManageContractRequestResultCode::MALFORMED);
        return false;
    }


    if (mManageContractRequest.details.contractRequest().startTime >=
        mManageContractRequest.details.contractRequest().endTime)
    {
        innerResult().code(ManageContractRequestResultCode::MALFORMED);
        return false;
    }

    return true;
}

