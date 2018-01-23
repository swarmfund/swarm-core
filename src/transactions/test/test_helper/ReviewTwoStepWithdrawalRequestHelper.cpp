// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include <ledger/StatisticsHelper.h>
#include "ReviewTwoStepWithdrawalRequestHelper.h"
#include "ledger/AssetFrame.h"
#include "ledger/AssetHelper.h"
#include "ledger/BalanceFrame.h"
#include "ledger/BalanceHelper.h"
#include "ledger/ReviewableRequestHelper.h"
#include "test/test_marshaler.h"

namespace stellar
{
namespace txtest
{

TwoStepWithdrawReviewChecker::TwoStepWithdrawReviewChecker(TestManager::pointer testManager, const uint64_t requestID) : ReviewChecker(testManager)
{
    auto request = ReviewableRequestHelper::Instance()->loadRequest(requestID, mTestManager->getDB());
    if (!request)
    {
        return;
    }

    switch(request->getRequestType())
    {
    case ReviewableRequestType::WITHDRAW:
        withdrawalRequest = std::make_shared<WithdrawalRequest>(request->getRequestEntry().body.withdrawalRequest());
        break;
    case ReviewableRequestType::TWO_STEP_WITHDRAWAL:
        withdrawalRequest = std::make_shared<WithdrawalRequest>(request->getRequestEntry().body.twoStepWithdrawalRequest());
        break;
    default:
        return;
    }

    balanceBeforeTx = BalanceHelper::Instance()->loadBalance(withdrawalRequest->balance, mTestManager->getDB());
    if (balanceBeforeTx)
    {
        commissionBalanceBeforeTx = BalanceHelper::Instance()->loadBalance(mTestManager->getApp().getCommissionID(),
            balanceBeforeTx->getAsset(), mTestManager->getDB(), nullptr);
        assetBeforeTx = AssetHelper::Instance()->loadAsset(balanceBeforeTx->getAsset(), mTestManager->getDB());
    }

    AccountID requestor = request->getRequestor();
    statsBeforeTx = StatisticsHelper::Instance()->loadStatistics(requestor, mTestManager->getDB());
}

void TwoStepWithdrawReviewChecker::checkApprove(ReviewableRequestFrame::pointer request)
{
    REQUIRE(!!withdrawalRequest);
    auto requestAfterTx = ReviewableRequestHelper::Instance()->loadRequest(request->getRequestID(), mTestManager->getDB());
    REQUIRE(!!requestAfterTx);
    // request after tx must have been switched to withdrawal
    REQUIRE(requestAfterTx->getRequestType() == ReviewableRequestType::WITHDRAW);
    REQUIRE(requestAfterTx->getRequestEntry().body.withdrawalRequest().preConfirmationDetails == ReviewTwoStepWithdrawRequestHelper::externalDetails);
    // check balance
    REQUIRE(!!balanceBeforeTx);
    auto balanceHelper = BalanceHelper::Instance();
    auto balanceAfterTx = balanceHelper->loadBalance(withdrawalRequest->balance,
        mTestManager->getDB());
    REQUIRE(!!balanceAfterTx);
    REQUIRE(balanceAfterTx->mEntry == balanceBeforeTx->mEntry);

    // check commission
    REQUIRE(!!commissionBalanceBeforeTx);
    auto commissionBalanceAfterTx = balanceHelper->loadBalance(commissionBalanceBeforeTx->getBalanceID(), mTestManager->getDB());
    REQUIRE(!!commissionBalanceAfterTx);
    REQUIRE(commissionBalanceAfterTx->mEntry == commissionBalanceBeforeTx->mEntry);

    // check asset
    REQUIRE(!!assetBeforeTx);
    auto assetAfterTx = AssetHelper::Instance()->loadAsset(balanceAfterTx->getAsset(), mTestManager->getDB());
    REQUIRE(!!assetAfterTx);
    REQUIRE(assetBeforeTx->mEntry == assetAfterTx->mEntry);
}

void TwoStepWithdrawReviewChecker::checkPermanentReject(
    ReviewableRequestFrame::pointer request)
{
    auto balanceAfterTx = BalanceHelper::Instance()->loadBalance(withdrawalRequest->balance, mTestManager->getDB());
    REQUIRE(balanceAfterTx->getAmount() == balanceBeforeTx->getAmount() + withdrawalRequest->amount + withdrawalRequest->fee.fixed + withdrawalRequest->fee.percent);
    REQUIRE(balanceBeforeTx->getLocked() == balanceAfterTx->getLocked() + withdrawalRequest->amount + withdrawalRequest->fee.fixed + withdrawalRequest->fee.percent);
    
    auto assetAfterTx = AssetHelper::Instance()->loadAsset(balanceBeforeTx->getAsset(), mTestManager->getDB());
    REQUIRE(assetAfterTx->getIssued() == assetBeforeTx->getIssued());

    AccountID requestor = request->getRequestor();
    auto statsAfterTx = StatisticsHelper::Instance()->mustLoadStatistics(requestor, mTestManager->getDB());

    uint64_t universalAmount = withdrawalRequest->universalAmount;
    REQUIRE(statsAfterTx->getDailyOutcome() == statsBeforeTx->getDailyOutcome() - universalAmount);
    REQUIRE(statsAfterTx->getWeeklyOutcome() == statsBeforeTx->getWeeklyOutcome() - universalAmount);
    REQUIRE(statsAfterTx->getMonthlyOutcome() == statsBeforeTx->getMonthlyOutcome() - universalAmount);
    REQUIRE(statsAfterTx->getAnnualOutcome() == statsBeforeTx->getAnnualOutcome() - universalAmount);
}

TransactionFramePtr ReviewTwoStepWithdrawRequestHelper::createReviewRequestTx(
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
    reviewRequestOp.requestDetails.twoStepWithdrawal().externalDetails = externalDetails;
    return txFromOperation(source, op, nullptr);
}

ReviewTwoStepWithdrawRequestHelper::ReviewTwoStepWithdrawRequestHelper(
    TestManager::pointer testManager) : ReviewRequestHelper(testManager)
{
    requestMustBeDeletedAfterApproval = false;
}

std::string ReviewTwoStepWithdrawRequestHelper::externalDetails = "Updated external details on review";

ReviewRequestResult ReviewTwoStepWithdrawRequestHelper::applyReviewRequestTx(
    Account& source, uint64_t requestID, Hash requestHash,
    ReviewableRequestType requestType, ReviewRequestOpAction action,
    std::string rejectReason, ReviewRequestResultCode expectedResult)
{
    auto checker = TwoStepWithdrawReviewChecker(mTestManager, requestID);
    return ReviewRequestHelper::applyReviewRequestTx(source, requestID,
                                                     requestHash, requestType,
                                                     action, rejectReason,
                                                     expectedResult,
                                                     checker
                                                    );
}
}
}
