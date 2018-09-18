// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ReviewPreIssuanceRequestHelper.h"
#include "ledger/AssetFrame.h"
#include "ledger/AssetHelperLegacy.h"
#include "ledger/ReviewableRequestHelper.h"
#include "test/test_marshaler.h"



namespace stellar
{

namespace txtest
{
ReviewPreIssuanceChecker::ReviewPreIssuanceChecker(
    const TestManager::pointer& testManager, const uint64_t requestID) : ReviewChecker(testManager)
{
    auto reviewableRequestHelper = ReviewableRequestHelper::Instance();
    auto request = reviewableRequestHelper->loadRequest(requestID, mTestManager->getDB());
    if (!request || request->getType() != ReviewableRequestType::PRE_ISSUANCE_CREATE) {
        return;
    }
    preIssuanceRequest = std::make_shared<PreIssuanceRequest>(request->getRequestEntry().body.preIssuanceRequest());
    assetFrameBeforeTx = AssetHelperLegacy::Instance()->loadAsset(preIssuanceRequest->asset, mTestManager->getDB());
}

void ReviewPreIssuanceChecker::checkApprove(ReviewableRequestFrame::pointer requestBeforeTx)
{
    REQUIRE(!!assetFrameBeforeTx);
    auto assetHelper = AssetHelperLegacy::Instance();
    auto assetFrameAfterTx = assetHelper->loadAsset(preIssuanceRequest->asset, mTestManager->getDB());
    REQUIRE(assetFrameAfterTx->getAvailableForIssuance() == assetFrameBeforeTx->getAvailableForIssuance() + preIssuanceRequest->amount);
}

ReviewPreIssuanceChecker::ReviewPreIssuanceChecker(const TestManager::pointer &testManager,
                                                   std::shared_ptr<PreIssuanceRequest> request) : ReviewChecker(testManager)
{
    preIssuanceRequest = request;
    assetFrameBeforeTx = AssetHelperLegacy::Instance()->loadAsset(preIssuanceRequest->asset, mTestManager->getDB());
}

ReviewPreIssuanceRequestHelper::ReviewPreIssuanceRequestHelper(TestManager::pointer testManager) : ReviewRequestHelper(testManager)
{
}

ReviewRequestResult ReviewPreIssuanceRequestHelper::applyReviewRequestTx(Account & source, uint64_t requestID, Hash requestHash,
    ReviewableRequestType requestType, ReviewRequestOpAction action, std::string rejectReason, ReviewRequestResultCode expectedResult)
{
    auto reviewPreIssuanceChecker = ReviewPreIssuanceChecker(mTestManager, requestID);
    return ReviewRequestHelper::applyReviewRequestTx(source, requestID, requestHash, requestType, action, rejectReason, expectedResult,
                reviewPreIssuanceChecker);
}

ReviewRequestResult ReviewPreIssuanceRequestHelper::applyReviewRequestTx(Account & source, uint64_t requestID,
                                                                         ReviewRequestOpAction action, std::string rejectReason,
                                                                         ReviewRequestResultCode expectedResult)
{
    auto reviewableRequestHelper = ReviewableRequestHelper::Instance();
    auto request = reviewableRequestHelper->loadRequest(requestID, mTestManager->getDB());
    REQUIRE(request);
    return applyReviewRequestTx(source, requestID, request->getHash(), request->getRequestType(), action, rejectReason, expectedResult);
}


}

}
