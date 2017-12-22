// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ReviewSaleRequestHelper.h"
#include "ledger/AssetFrame.h"
#include "ledger/AssetHelper.h"
#include "ledger/BalanceHelper.h"
#include "ledger/ReviewableRequestHelper.h"
#include "ledger/SaleFrame.h"


namespace stellar
{
namespace txtest
{
SaleReviewChecker::SaleReviewChecker(const TestManager::pointer testManager,
                                     const uint64_t requestID) : ReviewChecker(testManager)
{
    auto request = ReviewableRequestHelper::Instance()->loadRequest(requestID, mTestManager->getDB());
    if (!request || request->getType() != ReviewableRequestType::SALE)
    {
        return;
    }

    saleCreationRequest = std::make_shared<SaleCreationRequest>(request->getRequestEntry().body.saleCreationRequest());
    baseAssetBeforeTx = AssetHelper::Instance()->loadAsset(saleCreationRequest->baseAsset, mTestManager->getDB());
}

void SaleReviewChecker::checkApprove(ReviewableRequestFrame::pointer)
{
    REQUIRE(!!saleCreationRequest);
    REQUIRE(!!baseAssetBeforeTx);
    auto baseAssetAfterTx = AssetHelper::Instance()->loadAsset(saleCreationRequest->baseAsset, mTestManager->getDB());
    REQUIRE(!!baseAssetAfterTx);
    uint64_t softCapInBaseAsset;
    const auto saleRequest = *saleCreationRequest;
    REQUIRE(SaleFrame::calculateRequiredBaseAssetForSoftCap(saleRequest, softCapInBaseAsset));
    REQUIRE(baseAssetBeforeTx->getLockedIssuance() + softCapInBaseAsset == baseAssetAfterTx->getLockedIssuance());
}

ReviewSaleRequestHelper::ReviewSaleRequestHelper(
    TestManager::pointer testManager) : ReviewRequestHelper(testManager)
{
}

ReviewRequestResult ReviewSaleRequestHelper::applyReviewRequestTx(
    Account& source, uint64_t requestID, Hash requestHash,
    ReviewableRequestType requestType, ReviewRequestOpAction action,
    std::string rejectReason, ReviewRequestResultCode expectedResult)
{
    auto checker = SaleReviewChecker(mTestManager, requestID);
    return ReviewRequestHelper::applyReviewRequestTx(source, requestID,
        requestHash, requestType,
        action, rejectReason,
        expectedResult,
        checker
    );
}
}
}
