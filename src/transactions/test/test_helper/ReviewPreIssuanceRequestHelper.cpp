// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ReviewPreIssuanceRequestHelper.h"
#include "ledger/AssetFrame.h"
#include "ledger/AssetHelper.h"
#include "ledger/ReviewableRequestHelper.h"



namespace stellar
{

namespace txtest
{
	void ReviewPreIssuanceRequestHelper::checkApproval(ReviewableRequestFrame::pointer requestBeforeTx, AssetFrame::pointer assetFrameBeforeTx)
	{
		REQUIRE(!!requestBeforeTx);
		REQUIRE(!!assetFrameBeforeTx);
		auto preIssuanceRequest = requestBeforeTx->getRequestEntry().body.preIssuanceRequest();
		auto assetHelper = AssetHelper::Instance();
		auto assetFrameAfterTx = assetHelper->loadAsset(preIssuanceRequest.asset, mTestManager->getDB());
		REQUIRE(assetFrameAfterTx->getAvailableForIssuance() == assetFrameBeforeTx->getAvailableForIssuance() + preIssuanceRequest.amount);
	}


	AssetFrame::pointer ReviewPreIssuanceRequestHelper::tryLoadAssetFrameForRequest(uint64_t requestID)
	{
		auto reviewableRequestHelper = ReviewableRequestHelper::Instance();
		auto request = reviewableRequestHelper->loadRequest(requestID, mTestManager->getDB());
		if (!request) {
			return nullptr;
		}

		if (request->getType() != ReviewableRequestType::PRE_ISSUANCE_CREATE) {
			return nullptr;
		}

		auto requestEntry = request->getRequestEntry();
		auto assetHelper = AssetHelper::Instance();
		return assetHelper->loadAsset(requestEntry.body.preIssuanceRequest().asset, mTestManager->getDB());
	}
	ReviewPreIssuanceRequestHelper::ReviewPreIssuanceRequestHelper(TestManager::pointer testManager) : ReviewRequestHelper(testManager)
	{
	}

	ReviewRequestResult ReviewPreIssuanceRequestHelper::applyReviewRequestTx(Account & source, uint64_t requestID, Hash requestHash,
		ReviewableRequestType requestType, ReviewRequestOpAction action, std::string rejectReason, ReviewRequestResultCode expectedResult)
	{
		auto assetFrameBeforeTx = tryLoadAssetFrameForRequest(requestID);
		return ReviewRequestHelper::applyReviewRequestTx(source, requestID, requestHash, requestType, action, rejectReason, expectedResult,
			[this, assetFrameBeforeTx](ReviewableRequestFrame::pointer request) {
			this->checkApproval(request, assetFrameBeforeTx);
		});
	}

	ReviewRequestResult ReviewPreIssuanceRequestHelper::applyReviewRequestTx(Account & source, uint64_t requestID, ReviewRequestOpAction action, std::string rejectReason, ReviewRequestResultCode expectedResult)
	{
		auto reviewableRequestHelper = ReviewableRequestHelper::Instance();
		auto request = reviewableRequestHelper->loadRequest(requestID, mTestManager->getDB());
		REQUIRE(request);
		return applyReviewRequestTx(source, requestID, request->getHash(), request->getRequestType(), action, rejectReason, expectedResult);
	}


}

}
