#pragma once

// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "overlay/StellarXDR.h"
#include "ReviewRequestTestHelper.h"
#include "ledger/ReviewableRequestFrame.h"
#include "ledger/AssetFrame.h"
#include "ledger/BalanceFrame.h"

namespace stellar
{
namespace txtest
{

class SaleReviewChecker : public ReviewChecker
{
    std::shared_ptr<SaleCreationRequest> saleCreationRequest;
    AssetFrame::pointer baseAssetBeforeTx;
public:
    SaleReviewChecker(TestManager::pointer testManager, uint64_t requestID);
    void checkApprove(ReviewableRequestFrame::pointer) override;
};

class ReviewSaleRequestHelper : public ReviewRequestHelper
{

public:
    ReviewSaleRequestHelper(TestManager::pointer testManager);

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
