// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ReviewIssuanceRequestHelper.h"
#include "ledger/AssetFrame.h"
#include "ledger/BalanceFrame.h"



namespace stellar
{

namespace txtest
{
	std::shared_ptr<IssuanceRequest> ReviewIssuanceRequestHelper::tryLoadIssuanceRequest(uint64_t requestID)
	{
		auto request = ReviewableRequestFrame::loadRequest(requestID, mTestManager->getDB());
		if (!request) {
			return nullptr;
		}

		if (request->getType() != ReviewableRequestType::ISSUANCE_CREATE) {
			return nullptr;
		}

		return std::make_shared<IssuanceRequest>(request->getRequestEntry().body.issuanceRequest());
	}

	void ReviewIssuanceRequestHelper::checkApproval(IssuanceRequest issuanceRequest, AssetFrame::pointer assetFrameBeforeTx,
		BalanceFrame::pointer balanceBeforeTx)
	{
		// check asset
		REQUIRE(!!assetFrameBeforeTx);
		auto assetFrameAfterTx = AssetFrame::loadAsset(issuanceRequest.asset, mTestManager->getDB());
		REQUIRE(!!assetFrameAfterTx);
		REQUIRE(assetFrameAfterTx->getAvailableForIssuance() == assetFrameBeforeTx->getAvailableForIssuance() - issuanceRequest.amount);
		REQUIRE(assetFrameAfterTx->getIssued() == assetFrameBeforeTx->getIssued() + issuanceRequest.amount);
		// check balance
		REQUIRE(!!balanceBeforeTx);
		auto balanceAfterTx = BalanceFrame::loadBalance(issuanceRequest.receiver, mTestManager->getDB());
		REQUIRE(!!balanceAfterTx);
		REQUIRE(balanceAfterTx->getAmount() == balanceBeforeTx->getAmount() + issuanceRequest.amount);
	}


	AssetFrame::pointer ReviewIssuanceRequestHelper::tryLoadAssetFrameForRequest(uint64_t requestID)
	{
		auto issuanceRequest = tryLoadIssuanceRequest(requestID);
		if (issuanceRequest == nullptr) {
			return nullptr;
		}
		return AssetFrame::loadAsset(issuanceRequest->asset, mTestManager->getDB());
	}

	BalanceFrame::pointer ReviewIssuanceRequestHelper::tryLoadBalanceForRequest(uint64_t requestID)
	{
		auto issuanceRequest = tryLoadIssuanceRequest(requestID);
		if (issuanceRequest == nullptr) {
			return nullptr;
		}

		return BalanceFrame::loadBalance(issuanceRequest->receiver, mTestManager->getDB());
	}

	ReviewIssuanceRequestHelper::ReviewIssuanceRequestHelper(TestManager::pointer testManager) : ReviewRequestHelper(testManager)
	{
	}

	ReviewRequestResult ReviewIssuanceRequestHelper::applyReviewRequestTx(Account & source, uint64_t requestID, Hash requestHash,
		ReviewableRequestType requestType, ReviewRequestOpAction action, std::string rejectReason, ReviewRequestResultCode expectedResult)
	{
		auto assetFrameBeforeTx = tryLoadAssetFrameForRequest(requestID);
		auto balanceFrameBeForeTx = tryLoadBalanceForRequest(requestID);
		return ReviewRequestHelper::applyReviewRequestTx(source, requestID, requestHash, requestType, action, rejectReason, expectedResult,
			[this, assetFrameBeforeTx, balanceFrameBeForeTx](ReviewableRequestFrame::pointer request) {
			REQUIRE(!!request);
			REQUIRE(request->getRequestEntry().body.type() == ReviewableRequestType::ISSUANCE_CREATE);
			this->checkApproval(request->getRequestEntry().body.issuanceRequest(), assetFrameBeforeTx, balanceFrameBeForeTx);
		});
	}

	ReviewRequestResult ReviewIssuanceRequestHelper::applyReviewRequestTx(Account & source, uint64_t requestID, ReviewRequestOpAction action, std::string rejectReason, ReviewRequestResultCode expectedResult)
	{
		auto request = ReviewableRequestFrame::loadRequest(requestID, mTestManager->getDB());
		REQUIRE(request);
		return applyReviewRequestTx(source, requestID, request->getHash(), request->getRequestType(), action, rejectReason, expectedResult);
	}


}

}
