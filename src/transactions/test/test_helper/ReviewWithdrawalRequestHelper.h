#pragma once

// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "overlay/StellarXDR.h"
#include "ReviewRequestTestHelper.h"
#include "ledger/ReviewableRequestFrame.h"
#include "ledger/AssetFrame.h"
#include "ledger/BalanceFrame.h"
#include "ReviewTwoStepWithdrawalRequestHelper.h"

namespace stellar
{
namespace txtest
{

class WithdrawReviewChecker : public TwoStepWithdrawReviewChecker
{
public:
    WithdrawReviewChecker(TestManager::pointer testManager, uint64_t requestID);
    void checkApprove(ReviewableRequestFrame::pointer) override;
};

class ReviewWithdrawRequestHelper : public ReviewRequestHelper
{

public:
    explicit ReviewWithdrawRequestHelper(TestManager::pointer testManager);

    using ReviewRequestHelper::applyReviewRequestTx;
    ReviewRequestResult applyReviewRequestTx(Account& source,
                                             uint64_t requestID,
                                             Hash requestHash,
                                             ReviewableRequestType requestType,
                                             ReviewRequestOpAction action,
                                             std::string rejectReason,
                                             ReviewRequestResultCode
                                             expectedResult =
                                                 ReviewRequestResultCode::
                                                 SUCCESS) override;

    TransactionFramePtr createReviewRequestTx(Account& source,
        uint64_t requestID, Hash requestHash, ReviewableRequestType requestType,
        ReviewRequestOpAction action, std::string rejectReason) override;
};
}
}
