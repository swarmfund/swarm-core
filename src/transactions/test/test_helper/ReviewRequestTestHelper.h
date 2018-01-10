#pragma once

// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "overlay/StellarXDR.h"
#include "TxHelper.h"
#include "ledger/ReviewableRequestFrame.h"

namespace stellar
{
namespace txtest
{
class ReviewChecker
{
protected:
    TestManager::pointer mTestManager;
    Operation mOperation;
public:
    ReviewChecker(TestManager::pointer testManager)
    {
        mTestManager = testManager;
    }

    virtual void checkApprove(ReviewableRequestFrame::pointer)
    {
    }

    virtual void checkReject(ReviewableRequestFrame::pointer requestBeforeTx,
                             ReviewableRequestFrame::pointer requestAfterTx)
    {
    }

    virtual void checkPermanentReject(ReviewableRequestFrame::pointer)
    {
    }

    void setOperation(Operation &op)
    {
        mOperation = op;
    }
};

class ReviewRequestHelper : public TxHelper
{
protected:
    ReviewRequestHelper(TestManager::pointer testManager);


    ReviewRequestResult applyReviewRequestTx(Account& source,
                                             uint64_t requestID,
                                             Hash requestHash,
                                             ReviewableRequestType requestType,
                                             ReviewRequestOpAction action,
                                             std::string rejectReason,
                                             ReviewRequestResultCode
                                             expectedResult,
                                             ReviewChecker& checker);

public:

    virtual ReviewRequestResult applyReviewRequestTx(Account& source, uint64_t requestID, ReviewRequestOpAction action, std::string rejectReason,
        ReviewRequestResultCode expectedResult = ReviewRequestResultCode::SUCCESS);

    virtual ReviewRequestResult applyReviewRequestTx(
        Account& source, uint64_t requestID, Hash requestHash,
        ReviewableRequestType requestType,
        ReviewRequestOpAction action, std::string rejectReason,
        ReviewRequestResultCode expectedResult = ReviewRequestResultCode::
            SUCCESS) = 0;

    virtual TransactionFramePtr createReviewRequestTx(
        Account& source, uint64_t requestID, Hash requestHash,
        ReviewableRequestType requestType,
        ReviewRequestOpAction action, std::string rejectReason);
};
}
}
