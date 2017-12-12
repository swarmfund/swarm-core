// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "util/asio.h"
#include "ReviewWithdrawalRequestOpFrame.h"
#include "util/Logging.h"
#include "util/types.h"
#include "database/Database.h"
#include "ledger/LedgerDelta.h"
#include "ledger/ReviewableRequestFrame.h"
#include "ledger/ReferenceFrame.h"
#include "ledger/AssetHelper.h"
#include "ledger/BalanceHelper.h"
#include "main/Application.h"
#include "xdrpp/printer.h"
#include "ledger/ReviewableRequestHelper.h"

namespace stellar
{
using namespace std;
using xdr::operator==;

bool ReviewWithdrawalRequestOpFrame::handleApprove(
    Application& app, LedgerDelta& delta, LedgerManager& ledgerManager,
    ReviewableRequestFrame::pointer request)
{
    if (request->getRequestType() != ReviewableRequestType::WITHDRAW)
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) <<
            "Unexpected request type. Expected WITHDRAW, but got " << xdr::
            xdr_traits<ReviewableRequestType>::
            enum_name(request->getRequestType());
        throw std::
            invalid_argument("Unexpected request type for review withdraw request");
    }

    auto& withdrawRequest = request->getRequestEntry().body.withdrawalRequest();
    withdrawRequest.externalDetails = mReviewRequest.requestDetails.withdrawal()
                                                    .externalDetails;
    request->recalculateHashRejectReason();

    Database& db = ledgerManager.getDatabase();
    // update is required here to update reviewable request in horizon
    ReviewableRequestHelper::Instance()->
        storeChange(delta, db, request->mEntry);
    EntryHelperProvider::storeDeleteEntry(delta, db, request->getKey());

    uint64_t totalFee;
    if (!safeSum(withdrawRequest.fee.percent, withdrawRequest.fee.fixed, totalFee))
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Failed to calculate total fee for withdrawal request: " << request->getRequestID();
        throw runtime_error("Failed to calculate total fee for withdrawal request");
    }

    uint64_t totalAmountToCharge;
    if (!safeSum(withdrawRequest.amount, totalFee, totalAmountToCharge))
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Failed to calculate total amount ot be charged for withdrawal request: " << request->getRequestID();
        throw runtime_error("Failed to calculate total amount to be charged for withdrawal request");
    }


    auto balance = BalanceHelper::Instance()->mustLoadBalance(withdrawRequest.balance, db, &delta);
    if (!balance->tryChargeFromLocked(totalAmountToCharge))
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected db state. Failed to charge from locked amount which must be locked. requestID: " << request->getRequestID();
        throw std::runtime_error("Unexected db state. Failed to charge from locked");
    }
    EntryHelperProvider::storeChangeEntry(delta, db, balance->mEntry);

    auto commissionBalance = BalanceHelper::Instance()->loadBalance(app.getCommissionID(), balance->getAsset(), db, &delta);
    if (!commissionBalance)
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected state: reviewing withdrwal requet - and there is no commission balance for that asset" << request->getRequestID();
        throw std::runtime_error("Unexpected state: no commission balance");
    }

    if (!commissionBalance->tryFundAccount(totalFee))
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Failed to fund commission balance with fee for withdrawal - overflow" << request->getRequestID();
        throw runtime_error("Failed to fund commission balance with fee");
    }

    EntryHelperProvider::storeChangeEntry(delta, db, commissionBalance->mEntry);

    auto assetFrame = AssetHelper::Instance()->loadAsset(balance->getAsset(), db, &delta);
    if (!assetFrame)
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Failed to load asset for withdrawal request" << request->getRequestID();
        throw runtime_error("Failed to load asset for withdrawal request");
    }

    if (!assetFrame->tryWithdraw(withdrawRequest.amount))
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected state: failed to withdraw from asset for request: " << request->getRequestID();
        throw runtime_error("Failed to withdraw from asset");
    }

    AssetHelper::Instance()->storeChange(delta, db, assetFrame->mEntry);

    innerResult().code(ReviewRequestResultCode::SUCCESS);
    return true;
}

bool ReviewWithdrawalRequestOpFrame::handleReject(
    Application& app, LedgerDelta& delta, LedgerManager& ledgerManager,
    ReviewableRequestFrame::pointer request)
{
    innerResult().code(ReviewRequestResultCode::REJECT_NOT_ALLOWED);
    return false;
}

SourceDetails ReviewWithdrawalRequestOpFrame::getSourceAccountDetails(
    std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails)
const
{
    return SourceDetails({AccountType::MASTER},
                         mSourceAccount->getHighThreshold(),
                         static_cast<int32_t>(SignerType::ASSET_MANAGER));
}

ReviewWithdrawalRequestOpFrame::ReviewWithdrawalRequestOpFrame(
    Operation const& op, OperationResult& res, TransactionFrame& parentTx) :
                                                                           ReviewRequestOpFrame(op,
                                                                                                res,
                                                                                                parentTx)
{
}
}
