// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ReviewAssetRequestHelper.h"
#include "ledger/AssetFrame.h"



namespace stellar
{

namespace txtest
{
	void ReviewAssetRequestHelper::checkApproval(AssetCreationRequest const & request, AccountID const& requestor)
	{
		auto& delta = mTestManager->getLedgerDelta();
		auto assetFrame = AssetFrame::loadAsset(request.code, mTestManager->getDB(), &delta);
		REQUIRE(!!assetFrame);
		auto assetEntry = assetFrame->getAsset();
		REQUIRE(assetEntry.availableForIssueance == 0);
		REQUIRE(assetEntry.code == request.code);
		REQUIRE(assetEntry.description == request.description);
		REQUIRE(assetEntry.externalResourceLink == request.externalResourceLink);
		REQUIRE(assetEntry.issued == 0);
		REQUIRE(assetEntry.maxIssuanceAmount == request.maxIssuanceAmount);
		REQUIRE(assetEntry.name == request.name);
		REQUIRE(assetEntry.owner == requestor);
		REQUIRE(assetEntry.policies == request.policies);
		REQUIRE(assetEntry.maxIssuanceAmount == request.maxIssuanceAmount);
	}

	void ReviewAssetRequestHelper::checkApproval(AssetUpdateRequest const & request, AccountID const & requestor)
	{
		auto& delta = mTestManager->getLedgerDelta();
		auto assetFrame = AssetFrame::loadAsset(request.code, mTestManager->getDB(), &delta);
		REQUIRE(!!assetFrame);
		auto assetEntry = assetFrame->getAsset();
		REQUIRE(assetEntry.code == request.code);
		REQUIRE(assetEntry.description == request.description);
		REQUIRE(assetEntry.externalResourceLink == request.externalResourceLink);
		REQUIRE(assetEntry.owner == requestor);
		REQUIRE(assetEntry.policies == request.policies);
	}

	void ReviewAssetRequestHelper::checkApproval(ReviewableRequestFrame::pointer requestBeforeTx)
	{
		switch (requestBeforeTx->getRequestEntry().body.type()) {
		case ReviewableRequestType::ASSET_CREATE:
			checkApproval(requestBeforeTx->getRequestEntry().body.assetCreationRequest(), requestBeforeTx->getRequestor());
			break;
		case ReviewableRequestType::ASSET_UPDATE:
			checkApproval(requestBeforeTx->getRequestEntry().body.assetUpdateRequest(), requestBeforeTx->getRequestor());
			break;
		default:
			throw std::runtime_error("Unexecpted request type");
		}
	}

	ReviewAssetRequestHelper::ReviewAssetRequestHelper(TestManager::pointer testManager) : ReviewRequestHelper(testManager)
	{
	}
}

}
