#pragma once

// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "overlay/StellarXDR.h"
#include "ReviewRequestTestHelper.h"
#include "ledger/ReviewableRequestFrame.h"

namespace stellar
{
namespace txtest
{

    class AssetReviewChecker : public ReviewChecker
    {
    protected:
        void checkApproval(AssetCreationRequest const& request,
            AccountID const& requestor) const;
        void checkApproval(AssetUpdateRequest const& request,
            AccountID const& requestor);
    public:
        explicit AssetReviewChecker(const TestManager::pointer& testManager)
            : ReviewChecker(testManager)
        {
        }

        void checkApprove(ReviewableRequestFrame::pointer) override;
    };
class ReviewAssetRequestHelper : public ReviewRequestHelper
{
protected:
    void checkApproval(ReviewableRequestFrame::pointer requestBeforeTx);
public:
    explicit ReviewAssetRequestHelper(TestManager::pointer testManager);

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
};
}
}
