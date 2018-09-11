// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ledger/ReviewableRequestHelper.h"
#include "test/test_marshaler.h"
#include "ReviewContractRequestHelper.h"

namespace stellar
{
namespace txtest
{

ContractReviewChecker::ContractReviewChecker(TestManager::pointer testManager)
        : ReviewChecker(testManager)
{
}

void
ContractReviewChecker::checkApprove(ReviewableRequestFrame::pointer requestBeforeTx)
{
}

void
ContractReviewChecker::checkPermanentReject(ReviewableRequestFrame::pointer request)
{
}

ReviewContractRequestHelper::ReviewContractRequestHelper(TestManager::pointer testManager)
        : ReviewRequestHelper(testManager)
{
}

ReviewRequestResult
ReviewContractRequestHelper::applyReviewRequestTx(Account &source, uint64_t requestID, Hash requestHash,
                                                 ReviewableRequestType requestType,
                                                 ReviewRequestOpAction action,
                                                 std::string rejectReason,
                                                 ReviewRequestResultCode expectedResult)
{
    auto checker = ContractReviewChecker(mTestManager);
    requestMustBeDeletedAfterApproval = true;
    return ReviewRequestHelper::applyReviewRequestTx(source, requestID,
                                                     requestHash, requestType,
                                                     action, rejectReason,
                                                     expectedResult,
                                                     checker);
}

TransactionFramePtr
ReviewContractRequestHelper::createReviewRequestTx(Account& source,
                                                  uint64_t requestID, Hash requestHash,
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
    reviewRequestOp.requestDetails.contract().details = customerDetails;
    return txFromOperation(source, op, nullptr);
}

}
}