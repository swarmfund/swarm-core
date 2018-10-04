// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include <transactions/review_request/ReviewRequestOpFrame.h>
#include <ledger/FeeHelper.h>
#include <ledger/AccountHelper.h>
#include "ReviewIssuanceRequestHelper.h"
#include "ledger/AssetFrame.h"
#include "ledger/AssetHelperLegacy.h"
#include "ledger/BalanceFrame.h"
#include "ledger/BalanceHelperLegacy.h"
#include "ledger/ReviewableRequestHelper.h"
#include "test/test_marshaler.h"



namespace stellar
{

namespace txtest
{

ReviewIssuanceChecker::ReviewIssuanceChecker(
    const TestManager::pointer& testManager, const uint64_t requestID) : ReviewChecker(testManager)
{
    auto reviewableRequestHelper = ReviewableRequestHelper::Instance();
    auto request = reviewableRequestHelper->loadRequest(requestID, mTestManager->getDB());
    if (!request || request->getType() != ReviewableRequestType::ISSUANCE_CREATE) {
        return;
    }
    issuanceRequest = std::make_shared<IssuanceRequest>(request->getRequestEntry().body.issuanceRequest());
    assetFrameBeforeTx = AssetHelperLegacy::Instance()->loadAsset(issuanceRequest->asset, mTestManager->getDB());
    balanceBeforeTx = BalanceHelperLegacy::Instance()->loadBalance(issuanceRequest->receiver, mTestManager->getDB());
    commissionBalanceBeforeTx = BalanceHelperLegacy::Instance()->loadBalance(testManager->getApp().getCommissionID(),
                                                                       issuanceRequest->asset, testManager->getDB(),
                                                                       nullptr);    
}

ReviewIssuanceChecker::ReviewIssuanceChecker(
    const TestManager::pointer& testManager,
    std::shared_ptr<IssuanceRequest> request) : ReviewChecker(testManager)
{
    issuanceRequest = request;
    assetFrameBeforeTx = AssetHelperLegacy::Instance()->loadAsset(issuanceRequest->asset, mTestManager->getDB());
    balanceBeforeTx = BalanceHelperLegacy::Instance()->loadBalance(issuanceRequest->receiver, mTestManager->getDB());
    commissionBalanceBeforeTx = BalanceHelperLegacy::Instance()->loadBalance(testManager->getApp().getCommissionID(),
                                                                       issuanceRequest->asset, testManager->getDB(),
                                                                       nullptr);
}

void ReviewIssuanceChecker::checkApprove(ReviewableRequestFrame::pointer request)
{
    // checkApprove can be called during auto approve, so the request can be nullptr
    if (request != nullptr)
    {
        auto& requestEntry = request->getRequestEntry();
        // if a request has pending tasks - no need to check approval effects
        if (requestEntry.ext.v() == LedgerVersion::ADD_TASKS_TO_REVIEWABLE_REQUEST &&
            requestEntry.ext.tasksExt().pendingTasks != 0)
        {
            return;
        }
    }

    // check asset
    REQUIRE(!!issuanceRequest);
    REQUIRE(!!assetFrameBeforeTx);
    auto assetFrameAfterTx = AssetHelperLegacy::Instance()->loadAsset(issuanceRequest->asset, mTestManager->getDB());
    REQUIRE(!!assetFrameAfterTx);
    REQUIRE(assetFrameAfterTx->getAvailableForIssuance() == assetFrameBeforeTx->getAvailableForIssuance() - issuanceRequest->amount);
    REQUIRE(assetFrameAfterTx->getIssued() == assetFrameBeforeTx->getIssued() + issuanceRequest->amount);
    REQUIRE(!!balanceBeforeTx);
    auto receiverFrame = AccountHelper::Instance()->loadAccount(balanceBeforeTx->getAccountID(), mTestManager->getDB());
    auto feeFrame = FeeHelper::Instance()->loadForAccount(FeeType::ISSUANCE_FEE, issuanceRequest->asset,
                                                          FeeFrame::SUBTYPE_ANY, receiverFrame, issuanceRequest->amount,
                                                          mTestManager->getDB());
    uint64_t totalFee = 0;
    if (feeFrame) {
        REQUIRE(feeFrame->calculatePercentFee(issuanceRequest->amount, totalFee, ROUND_UP));
        totalFee += feeFrame->getFee().fixedFee;
        REQUIRE(!!commissionBalanceBeforeTx);
        auto commissionBalanceAfterTx = BalanceHelperLegacy::Instance()->loadBalance(mTestManager->getApp().getCommissionID(),
                                                                               issuanceRequest->asset, mTestManager->getDB(),
                                                                               nullptr);
        REQUIRE(!!commissionBalanceAfterTx);
        //check commission balance change
        REQUIRE(commissionBalanceAfterTx->getAmount() == commissionBalanceBeforeTx->getAmount() + totalFee);
    }

    // check balance
    auto balanceAfterTx = BalanceHelperLegacy::Instance()->loadBalance(issuanceRequest->receiver, mTestManager->getDB());
    REQUIRE(!!balanceAfterTx);
    uint64_t destinationReceive = issuanceRequest->amount - totalFee;
    REQUIRE(balanceAfterTx->getAmount() == balanceBeforeTx->getAmount() + destinationReceive);
}

ReviewIssuanceRequestHelper::ReviewIssuanceRequestHelper(TestManager::pointer testManager) : ReviewRequestHelper(testManager)
{
}

ReviewRequestResult ReviewIssuanceRequestHelper::applyReviewRequestTx(Account & source, uint64_t requestID, Hash requestHash,
    ReviewableRequestType requestType, ReviewRequestOpAction action, std::string rejectReason, ReviewRequestResultCode expectedResult)
{
        auto issuanceChecker = ReviewIssuanceChecker(mTestManager, requestID);
    return ReviewRequestHelper::applyReviewRequestTx(source, requestID, requestHash, requestType, action, rejectReason, expectedResult,
                issuanceChecker);
}

TransactionFramePtr
ReviewIssuanceRequestHelper::createReviewRequestTx(Account &source, uint64_t requestID, Hash requestHash,
                                                   ReviewableRequestType requestType, ReviewRequestOpAction action,
                                                   std::string rejectReason)
{
    Operation op;
    op.body.type(OperationType::REVIEW_REQUEST);
    ReviewRequestOp& reviewRequestOp = op.body.reviewRequestOp();
    reviewRequestOp.action = action;
    reviewRequestOp.reason = rejectReason;
    reviewRequestOp.requestHash = requestHash;
    reviewRequestOp.requestID = requestID;
    reviewRequestOp.requestDetails.requestType(requestType);
    reviewRequestOp.ext.v(LedgerVersion::ADD_TASKS_TO_REVIEWABLE_REQUEST);
    reviewRequestOp.ext.reviewDetails().tasksToAdd = 0;
    reviewRequestOp.ext.reviewDetails().tasksToRemove = action == ReviewRequestOpAction::APPROVE ? 8 : 0;
    reviewRequestOp.ext.reviewDetails().externalDetails = "{}";

    return txFromOperation(source, op, nullptr);
}

    ReviewRequestResult ReviewIssuanceRequestHelper::applyReviewRequestTx(Account & source, uint64_t requestID, ReviewRequestOpAction action, std::string rejectReason, ReviewRequestResultCode expectedResult)
{
    auto reviewableRequestHelper = ReviewableRequestHelper::Instance();
    auto request = reviewableRequestHelper->loadRequest(requestID, mTestManager->getDB());
    REQUIRE(request);
    return applyReviewRequestTx(source, requestID, request->getHash(), request->getRequestType(), action, rejectReason, expectedResult);
}


}

}
