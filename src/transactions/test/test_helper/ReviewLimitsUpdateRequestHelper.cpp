// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ReviewLimitsUpdateRequestHelper.h"
#include "ledger/ReviewableRequestHelper.h"
#include "test/test_marshaler.h"

namespace stellar
{
namespace txtest
{
LimitsUpdateReviewChecker::LimitsUpdateReviewChecker(TestManager::pointer testManager,
                                                     uint64_t requestID) : ReviewChecker(testManager)
{
    Database& db = mTestManager->getDB();

    auto reviewableRequestHelper = ReviewableRequestHelper::Instance();
    auto request = reviewableRequestHelper->loadRequest(requestID, db);
    if (!request || request->getType() != ReviewableRequestType::LIMITS_UPDATE)
    {
        return;
    }

    limitsUpdateRequest = std::make_shared<LimitsUpdateRequest>(request->getRequestEntry().body.limitsUpdateRequest());

    auto accountLimitsHelper = AccountLimitsHelper::Instance();
    AccountID requestor = request->getRequestor();
    accountLimitsBeforeTx = accountLimitsHelper->loadLimits(requestor, db);
    if (!accountLimitsBeforeTx)
    {
        return;
    }
}

void
LimitsUpdateReviewChecker::checkApprove(ReviewableRequestFrame::pointer request) {
    Database& db = mTestManager->getDB();

    REQUIRE(!!limitsUpdateRequest);

    // check accountLimits
    REQUIRE(!!accountLimitsBeforeTx);
    auto accountLimitsHelper = AccountLimitsHelper::Instance();
    AccountID requestor = request->getRequestor();
    auto accountLimitsAfterTx = accountLimitsHelper->loadLimits(requestor, db);
    REQUIRE(!!accountLimitsAfterTx);
}

void
LimitsUpdateReviewChecker::checkPermanentReject(ReviewableRequestFrame::pointer request)
{
    Database& db = mTestManager->getDB();

    auto accountLimitsHelper = AccountLimitsHelper::Instance();
    AccountID requestor = request->getRequestor();
    auto accountLimitsAfterTx = accountLimitsHelper->loadLimits(requestor, db);

    REQUIRE(accountLimitsBeforeTx->getLimits().annualOut == accountLimitsAfterTx->getLimits().annualOut);
    REQUIRE(accountLimitsBeforeTx->getLimits().dailyOut == accountLimitsAfterTx->getLimits().dailyOut);
    REQUIRE(accountLimitsBeforeTx->getLimits().monthlyOut == accountLimitsAfterTx->getLimits().monthlyOut);
    REQUIRE(accountLimitsBeforeTx->getLimits().weeklyOut == accountLimitsAfterTx->getLimits().weeklyOut);
}

ReviewLimitsUpdateRequestHelper::ReviewLimitsUpdateRequestHelper(
        TestManager::pointer testManager) : ReviewRequestHelper(testManager)
{
}

ReviewRequestResult
ReviewLimitsUpdateRequestHelper::applyReviewRequestTx(Account &source, uint64_t requestID, Hash requestHash,
                                                      ReviewableRequestType requestType,
                                                      ReviewRequestOpAction action,
                                                      std::string rejectReason,
                                                      ReviewRequestResultCode expectedResult)
{
    auto checker = LimitsUpdateReviewChecker(mTestManager, requestID);
    return ReviewRequestHelper::applyReviewRequestTx(source, requestID,
                                                     requestHash, requestType,
                                                     action, rejectReason,
                                                     expectedResult,
                                                     checker);
}

TransactionFramePtr
ReviewLimitsUpdateRequestHelper::createReviewRequestTx(Account &source, uint64_t requestID, Hash requestHash,
                                                       ReviewableRequestType requestType,
                                                       ReviewRequestOpAction action,
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
    reviewRequestOp.requestDetails.limitsUpdate().newLimits = createLimits(int64(100),int64(200),
                                                                           int64(300),int64(400));
    return txFromOperation(source, op, nullptr);
}

Limits
ReviewLimitsUpdateRequestHelper::createLimits(int64 dailyOut, int64 weeklyOut,
                                              int64 monthlyOut, int64 annualOut)
{
    Limits result;
    result.dailyOut = dailyOut;
    result.weeklyOut = weeklyOut;
    result.monthlyOut = monthlyOut;
    result.annualOut = annualOut;

    return result;
}

}
}