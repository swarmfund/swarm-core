// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include <ledger/ReviewableRequestFrame.h>
#include "transactions/ManageContractRequestOpFrame.h"
#include "database/Database.h"
#include "main/Application.h"
#include "medida/meter.h"
#include <crypto/SHA.h>
#include <ledger/ReviewableRequestHelper.h>
#include "medida/metrics_registry.h"
#include "ledger/LedgerDelta.h"
#include "ledger/BalanceHelper.h"

namespace stellar
{
using xdr::operator==;

std::unordered_map<AccountID, CounterpartyDetails>
ManageContractRequestOpFrame::getCounterpartyDetails(Database & db, LedgerDelta * delta) const
{
    return{
        {mSourceAccount->getID(),
         CounterpartyDetails({AccountType::GENERAL, AccountType::NOT_VERIFIED, AccountType::EXCHANGE,
                              AccountType::ACCREDITED_INVESTOR, AccountType::INSTITUTIONAL_INVESTOR,
                              AccountType::VERIFIED, AccountType::MASTER}, true, true)}
    };
}

SourceDetails
ManageContractRequestOpFrame::getSourceAccountDetails(
        std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
        int32_t ledgerVersion) const
{
    std::vector<AccountType> allowedAccountTypes = {AccountType::GENERAL, AccountType::NOT_VERIFIED,
                                                    AccountType::EXCHANGE, AccountType::ACCREDITED_INVESTOR,
                                                    AccountType::INSTITUTIONAL_INVESTOR, AccountType::VERIFIED,
                                                    AccountType::MASTER};

    return SourceDetails(allowedAccountTypes, mSourceAccount->getMediumThreshold(),
                         static_cast<int32_t>(SignerType::INVOICE_MANAGER),
                         static_cast<int32_t>(BlockReasons::KYC_UPDATE) |
                         static_cast<int32_t>(BlockReasons::TOO_MANY_KYC_UPDATE_REQUESTS));
}

ManageContractRequestOpFrame::ManageContractRequestOpFrame(Operation const& op, OperationResult& res,
        TransactionFrame& parentTx) : OperationFrame(op, res, parentTx),
        mManageContractRequest(mOperation.body.manageContractRequestOp())
{
}

std::string
ManageContractRequestOpFrame::getManageContractRequestReference(longstring const& details) const
{
    const auto hash = sha256(xdr::xdr_to_opaque(ReviewableRequestType::CONTRACT, details));
    return binToHex(hash);
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
    if (!reviewableRequest || reviewableRequest->getRequestType() != ReviewableRequestType::INVOICE)
    {
        innerResult().code(ManageContractRequestResultCode::NOT_FOUND);
        return false;
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

    auto reference = getManageContractRequestReference(contractRequest.details);

    auto reviewableRequestHelper = ReviewableRequestHelper::Instance();
    if (reviewableRequestHelper->isReferenceExist(db, getSourceID(), reference))
    {
        innerResult().code(ManageContractRequestResultCode::CONTRACT_REQUEST_REFERENCE_DUPLICATION);
        return false;
    }

    ReviewableRequestEntry::_body_t body;
    body.type(ReviewableRequestType::CONTRACT);
    body.contractRequest() = contractRequest;

    const auto referencePtr = xdr::pointer<string64>(new string64(reference));
    auto request = ReviewableRequestFrame::createNewWithHash(delta, getSourceID(), contractRequest.customer,
                                                             referencePtr, body, ledgerManager.getCloseTime());

    EntryHelperProvider::storeAddEntry(delta, db, request->mEntry);

    innerResult().success().details.action(ManageContractRequestAction::CREATE);
    innerResult().success().details.response().requestID = request->getRequestID();

    return true;
}

bool
ManageContractRequestOpFrame::doCheckValid(Application& app)
{
    if (mManageContractRequest.details.action() == ManageContractRequestAction::CREATE &&
        mManageContractRequest.details.contractRequest().details.empty())
    {
        app.getMetrics().NewMeter({"op-manage-invoice", "invalid", "malformed-zero-amount"},
                                  "operation").Mark();
        innerResult().code(ManageContractRequestResultCode::MALFORMED);
        return false;
    }

    return true;
}

}
