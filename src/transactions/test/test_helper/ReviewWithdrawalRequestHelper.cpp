// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ReviewWithdrawalRequestHelper.h"
#include "ledger/AssetFrame.h"
#include "ledger/AssetHelper.h"
#include "ledger/BalanceFrame.h"
#include "ledger/BalanceHelper.h"
#include "ledger/ReviewableRequestHelper.h"


namespace stellar
{
namespace txtest
{
WithdrawReviewChecker::WithdrawReviewChecker(TestManager::pointer testManager, const uint64_t requestID) : ReviewChecker(testManager)
{
    auto request = ReviewableRequestHelper::Instance()->loadRequest(requestID, mTestManager->getDB());
    if (!request || request->getType() != ReviewableRequestType::WITHDRAW)
    {
        return;
    }

    withdrawalRequest = std::make_shared<WithdrawalRequest>(request->getRequestEntry().body.withdrawalRequest());
    balanceBeforeTx = BalanceHelper::Instance()->loadBalance(withdrawalRequest->balance, mTestManager->getDB());
    if (!balanceBeforeTx)
    {
        return;
    }
    commissionBalanceBeforeTx = BalanceHelper::Instance()->loadBalance(mTestManager->getApp().getCommissionID(),
        balanceBeforeTx->getAsset(), mTestManager->getDB(), nullptr);
    assetBeforeTx = AssetHelper::Instance()->loadAsset(balanceBeforeTx->getAsset(), mTestManager->getDB());

}

void WithdrawReviewChecker::checkApprove(ReviewableRequestFrame::pointer)
{
    REQUIRE(!!withdrawalRequest);
    // check balance
    REQUIRE(!!balanceBeforeTx);
    auto balanceHelper = BalanceHelper::Instance();
    auto balanceAfterTx = balanceHelper->loadBalance(withdrawalRequest->balance,
        mTestManager->getDB());
    REQUIRE(!!balanceAfterTx);
    REQUIRE(balanceAfterTx->getAmount() == balanceBeforeTx->getAmount());
    REQUIRE(balanceBeforeTx->getLocked() == balanceAfterTx->getLocked() +
        withdrawalRequest->amount + withdrawalRequest->fee.fixed +
        withdrawalRequest->fee.percent);

    // check commission
    REQUIRE(!!commissionBalanceBeforeTx);
    auto commissionBalanceAfterTx = balanceHelper->loadBalance(commissionBalanceBeforeTx->getBalanceID(), mTestManager->getDB());
    REQUIRE(!!commissionBalanceAfterTx);
    REQUIRE(commissionBalanceAfterTx->getAmount() == commissionBalanceBeforeTx->getAmount() + withdrawalRequest->fee.fixed + withdrawalRequest->fee.percent);

    // check asset
    REQUIRE(!!assetBeforeTx);
    auto assetAfterTx = AssetHelper::Instance()->loadAsset(balanceAfterTx->getAsset(), mTestManager->getDB());
    REQUIRE(!!assetAfterTx);
    REQUIRE(assetBeforeTx->getIssued() == assetAfterTx->getIssued() + withdrawalRequest->amount);
    REQUIRE(assetBeforeTx->getAvailableForIssuance() == assetAfterTx->getAvailableForIssuance());
}

void WithdrawReviewChecker::checkPermanentReject(
    ReviewableRequestFrame::pointer)
{
    auto balanceAfterTx = BalanceHelper::Instance()->loadBalance(withdrawalRequest->balance, mTestManager->getDB());
    REQUIRE(balanceAfterTx->getAmount() == balanceBeforeTx->getAmount() + withdrawalRequest->amount + withdrawalRequest->fee.percent + withdrawalRequest->fee.percent);
    REQUIRE(balanceBeforeTx->getLocked() == balanceAfterTx->getLocked() + withdrawalRequest->amount + withdrawalRequest->fee.percent + withdrawalRequest->fee.percent);
    
    auto assetAfterTx = AssetHelper::Instance()->loadAsset(balanceBeforeTx->getAsset(), mTestManager->getDB());
    REQUIRE(assetAfterTx->getIssued() == assetBeforeTx->getIssued());
}

TransactionFramePtr ReviewWithdrawRequestHelper::createReviewRequestTx(
    Account& source, uint64_t requestID, Hash requestHash,
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
    reviewRequestOp.requestDetails.withdrawal().externalDetails = "Updated external details on review";
    return txFromOperation(source, op, nullptr);
}

ReviewWithdrawRequestHelper::ReviewWithdrawRequestHelper(
    TestManager::pointer testManager) : ReviewRequestHelper(testManager)
{
}

ReviewRequestResult ReviewWithdrawRequestHelper::applyReviewRequestTx(
    Account& source, uint64_t requestID, Hash requestHash,
    ReviewableRequestType requestType, ReviewRequestOpAction action,
    std::string rejectReason, ReviewRequestResultCode expectedResult)
{
    auto checker = WithdrawReviewChecker(mTestManager, requestID);
    return ReviewRequestHelper::applyReviewRequestTx(source, requestID,
                                                     requestHash, requestType,
                                                     action, rejectReason,
                                                     expectedResult,
        checker
                                                    );
}

ReviewRequestResult ReviewWithdrawRequestHelper::applyReviewRequestTx(
    Account& source, uint64_t requestID, ReviewRequestOpAction action,
    std::string rejectReason, ReviewRequestResultCode expectedResult)
{
    auto reviewableRequestHelper = ReviewableRequestHelper::Instance();
    auto request = reviewableRequestHelper->loadRequest(requestID,
                                                        mTestManager->getDB());
    REQUIRE(!!request);
    return applyReviewRequestTx(source, requestID, request->getHash(),
                                request->getRequestType(), action, rejectReason,
                                expectedResult);
}
}
}
