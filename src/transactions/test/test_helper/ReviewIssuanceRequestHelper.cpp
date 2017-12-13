// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ReviewIssuanceRequestHelper.h"
#include "ledger/AssetFrame.h"
#include "ledger/AssetHelper.h"
#include "ledger/BalanceFrame.h"
#include "ledger/BalanceHelper.h"
#include "ledger/ReviewableRequestHelper.h"



namespace stellar
{

namespace txtest
{

ReviewIssuanceChecker::ReviewIssuanceChecker(
    const TestManager::pointer& testManager, const uint64_t requestID) : ReviewChecker(testManager)
{
    auto reviewableRequestHelper = ReviewableRequestHelper::Instance();
    auto request = reviewableRequestHelper->loadRequest(requestID, mTestManager->getDB());
    if (!request || request->getType() != ReviewableRequestType::ISSUANCE_CREATE) {
        return;
    }
    issuanceRequest = std::make_shared<IssuanceRequest>(request->getRequestEntry().body.issuanceRequest());
    assetFrameBeforeTx = AssetHelper::Instance()->loadAsset(issuanceRequest->asset, mTestManager->getDB());
    balanceBeforeTx = BalanceHelper::Instance()->loadBalance(issuanceRequest->receiver, mTestManager->getDB());
}

ReviewIssuanceChecker::ReviewIssuanceChecker(
    const TestManager::pointer& testManager,
    std::shared_ptr<IssuanceRequest> request) : ReviewChecker(testManager)
{
    issuanceRequest = request;
    assetFrameBeforeTx = AssetHelper::Instance()->loadAsset(issuanceRequest->asset, mTestManager->getDB());
    balanceBeforeTx = BalanceHelper::Instance()->loadBalance(issuanceRequest->receiver, mTestManager->getDB());
}

void ReviewIssuanceChecker::checkApprove(ReviewableRequestFrame::pointer)
{
    // check asset
    REQUIRE(!!issuanceRequest);
    REQUIRE(!!assetFrameBeforeTx);
    auto assetFrameAfterTx = AssetHelper::Instance()->loadAsset(issuanceRequest->asset, mTestManager->getDB());
    REQUIRE(!!assetFrameAfterTx);
    REQUIRE(assetFrameAfterTx->getAvailableForIssuance() == assetFrameBeforeTx->getAvailableForIssuance() - issuanceRequest->amount);
    REQUIRE(assetFrameAfterTx->getIssued() == assetFrameBeforeTx->getIssued() + issuanceRequest->amount);
    // check balance
    REQUIRE(!!balanceBeforeTx);
    auto balanceAfterTx = BalanceHelper::Instance()->loadBalance(issuanceRequest->receiver, mTestManager->getDB());
    REQUIRE(!!balanceAfterTx);
    REQUIRE(balanceAfterTx->getAmount() == balanceBeforeTx->getAmount() + issuanceRequest->amount);
}

ReviewIssuanceRequestHelper::ReviewIssuanceRequestHelper(TestManager::pointer testManager) : ReviewRequestHelper(testManager)
	{
	}

	ReviewRequestResult ReviewIssuanceRequestHelper::applyReviewRequestTx(Account & source, uint64_t requestID, Hash requestHash,
		ReviewableRequestType requestType, ReviewRequestOpAction action, std::string rejectReason, ReviewRequestResultCode expectedResult)
	{
            auto issuanceChecker = ReviewIssuanceChecker(mTestManager, requestID);
		return ReviewRequestHelper::applyReviewRequestTx(source, requestID, requestHash, requestType, action, rejectReason, expectedResult,
                    issuanceChecker);
	}

	ReviewRequestResult ReviewIssuanceRequestHelper::applyReviewRequestTx(Account & source, uint64_t requestID, ReviewRequestOpAction action, std::string rejectReason, ReviewRequestResultCode expectedResult)
	{
		auto reviewableRequestHelper = ReviewableRequestHelper::Instance();
		auto request = reviewableRequestHelper->loadRequest(requestID, mTestManager->getDB());
		REQUIRE(request);
		return applyReviewRequestTx(source, requestID, request->getHash(), request->getRequestType(), action, rejectReason, expectedResult);
	}


}

}
