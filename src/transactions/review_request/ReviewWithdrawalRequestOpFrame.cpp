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
#include "ledger/AssetHelper.h"
#include "ledger/BalanceHelper.h"
#include "transactions/CreateWithdrawalRequestOpFrame.h"
#include "main/Application.h"
#include "xdrpp/printer.h"

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
        throw
            invalid_argument("Unexpected request type for review withdraw request");
    }

    auto& withdrawRequest = request->getRequestEntry().body.withdrawalRequest();

    Database& db = ledgerManager.getDatabase();
    EntryHelperProvider::storeDeleteEntry(delta, db, request->getKey());
 
    auto balance = BalanceHelper::Instance()->mustLoadBalance(withdrawRequest.balance, db, &delta);
    const auto totalAmountToCharge = getTotalAmountToCharge(request->getRequestID(), withdrawRequest);
    if (!balance->tryChargeFromLocked(totalAmountToCharge))
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected db state. Failed to charge from locked amount which must be locked. requestID: " << request->getRequestID();
        throw runtime_error("Unexected db state. Failed to charge from locked");
    }
    EntryHelperProvider::storeChangeEntry(delta, db, balance->mEntry);

    const uint64_t totalFee = getTotalFee(request->getRequestID(), withdrawRequest);
    AccountManager accountManager(app, db, delta, ledgerManager);
    accountManager.transferFee(balance->getAsset(), totalFee);

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

    ReviewWithdrawalRequestOpFrame::ReviewWithdrawalRequestOpFrame(
    Operation const& op, OperationResult& res, TransactionFrame& parentTx) :
        ReviewTwoStepWithdrawalRequestOpFrame(op,
                                                                                                res,
                                                                                                parentTx)
{
}

bool ReviewWithdrawalRequestOpFrame::handlePermanentReject(Application& app,
    LedgerDelta& delta, LedgerManager& ledgerManager,
    ReviewableRequestFrame::pointer request)
{
    if (request->getRequestType() != ReviewableRequestType::WITHDRAW)
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) <<
            "Unexpected request type. Expected WITHDRAW, but got " << xdr::
            xdr_traits<ReviewableRequestType>::
            enum_name(request->getRequestType());
        throw
            invalid_argument("Unexpected request type for review withdraw request");
    }

    auto& withdrawalRequest = request->getRequestEntry().body.withdrawalRequest();
    return rejectWithdrawalRequest(app, delta, ledgerManager, request, withdrawalRequest);
}

bool ReviewWithdrawalRequestOpFrame::doCheckValid(Application &app)
{
    auto withdrawalRequest  = mReviewRequest.requestDetails.withdrawal();

    if (!CreateWithdrawalRequestOpFrame::isExternalDetailsValid(app, withdrawalRequest.externalDetails,
                                                                withdrawalRequest.ext.v())) {
        innerResult().code(ReviewRequestResultCode::INVALID_EXTERNAL_DETAILS);
        return false;
    }

    return ReviewRequestOpFrame::doCheckValid(app);
}
}
