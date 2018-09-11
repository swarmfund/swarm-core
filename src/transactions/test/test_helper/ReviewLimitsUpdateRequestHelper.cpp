// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include <ledger/LimitsV2Helper.h>
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
    manageLimitsRequest = std::make_shared<LimitsUpdateRequest>(request->getRequestEntry().body.limitsUpdateRequest());
}

void
LimitsUpdateReviewChecker::checkApprove(ReviewableRequestFrame::pointer request) {
    Database& db = mTestManager->getDB();

    REQUIRE(!!manageLimitsRequest);

    // check accountLimits
    auto limitsHelper = LimitsV2Helper::Instance();
    auto limitsEntry = mOperation.body.reviewRequestOp().requestDetails.limitsUpdate().newLimitsV2;
    auto limitsAfterTx = limitsHelper->loadLimits(db, limitsEntry.statsOpType, limitsEntry.assetCode,
            limitsEntry.accountID, limitsEntry.accountType, limitsEntry.isConvertNeeded, nullptr);
    REQUIRE(!!limitsAfterTx);
    auto limitsEntryAfterTx = limitsAfterTx->getLimits();
    auto reviewRequestLimits = mOperation.body.reviewRequestOp().requestDetails.limitsUpdate().newLimitsV2;
    REQUIRE(limitsEntryAfterTx.dailyOut == reviewRequestLimits.dailyOut);
    REQUIRE(limitsEntryAfterTx.weeklyOut == reviewRequestLimits.weeklyOut);
    REQUIRE(limitsEntryAfterTx.monthlyOut == reviewRequestLimits.monthlyOut);
    REQUIRE(limitsEntryAfterTx.annualOut == reviewRequestLimits.annualOut);
}

void
LimitsUpdateReviewChecker::checkPermanentReject(ReviewableRequestFrame::pointer request)
{
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
    reviewRequestOp.requestDetails.limitsUpdate().newLimitsV2 = limitsV2Entry;
    return txFromOperation(source, op, nullptr);
}

void
ReviewLimitsUpdateRequestHelper::initializeLimits(AccountID& requestorID)
{
    limitsV2Entry.accountID.activate() = requestorID;
    limitsV2Entry.assetCode = "USD";
    limitsV2Entry.statsOpType = StatsOpType::PAYMENT_OUT;
    limitsV2Entry.isConvertNeeded = false;
    limitsV2Entry.dailyOut = 100;
    limitsV2Entry.weeklyOut = 200;
    limitsV2Entry.monthlyOut = 300;
    limitsV2Entry.annualOut = 500;
}

}
}