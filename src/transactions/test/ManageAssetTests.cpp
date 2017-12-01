// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0
#include "main/Config.h"
#include "overlay/LoopbackPeer.h"
#include "main/test.h"
#include "lib/catch.hpp"
#include "TxTests.h"
#include "test_helper/TestManager.h"
#include "test_helper/ManageAssetHelper.h"
#include "test_helper/ReviewAssetRequestHelper.h"
#include "test_helper/IssuanceRequestHelper.h"

using namespace stellar;
using namespace stellar::txtest;

void testManageAssetHappyPath(TestManager::pointer testManager, Account& account, Account& root) {
	SECTION("Can create asset")
	{
		auto preissuedSigner = SecretKey::random();
		auto manageAssetHelper = ManageAssetHelper(testManager);
		AssetCode assetCode = "EURT";
		auto creationRequest = manageAssetHelper.createAssetCreationRequest(assetCode, "New USD token", preissuedSigner.getPublicKey(), "Description can be quiete long", "https://testusd.usd", 0, 0);
		auto creationResult = manageAssetHelper.applyManageAssetTx(account, 0, creationRequest);
		SECTION("Can cancel creation request")
		{
			manageAssetHelper.applyManageAssetTx(account, creationResult.success().requestID, manageAssetHelper.createCancelRequest());
		}
		SECTION("Can update existing request")
		{
			creationRequest.createAsset().code = "USDT";
			auto updateResult = manageAssetHelper.applyManageAssetTx(account, creationResult.success().requestID, creationRequest);
			REQUIRE(updateResult.success().requestID == creationResult.success().requestID);
		}
		SECTION("Given approved asset")
		{
			LedgerDelta& delta = testManager->getLedgerDelta();
			auto approvingRequest = ReviewableRequestFrame::loadRequest(creationResult.success().requestID, testManager->getDB(), &delta);
			REQUIRE(approvingRequest);
			auto reviewRequetHelper = ReviewAssetRequestHelper(testManager);
			reviewRequetHelper.applyReviewRequestTx(root, approvingRequest->getRequestID(), approvingRequest->getHash(), approvingRequest->getType(),
				ReviewRequestOpAction::APPROVE, "");
			SECTION("Can update asset")
			{
				auto updateRequestBody = manageAssetHelper.createAssetUpdateRequest(assetCode, "Updated token descpition", "https://updatedlink.token", 0);
				auto updateResult = manageAssetHelper.applyManageAssetTx(account, 0, updateRequestBody);
				approvingRequest = ReviewableRequestFrame::loadRequest(updateResult.success().requestID, testManager->getDB(), &delta);
				REQUIRE(approvingRequest);
				reviewRequetHelper.applyReviewRequestTx(root, approvingRequest->getRequestID(), approvingRequest->getHash(), approvingRequest->getType(),
					ReviewRequestOpAction::APPROVE, "");
			}
		}
	}
}

TEST_CASE("manage asset", "[tx][manage_asset]")
{
    Config const& cfg = getTestConfig(0, Config::TESTDB_POSTGRESQL);
	VirtualClock clock;
	Application::pointer appPtr = Application::create(clock, cfg);
	Application& app = *appPtr;
	app.start();
	auto testManager = TestManager::make(app);

	auto root = Account{ getRoot(), Salt(0) };

	SECTION("Root happy path")
	{
		testManageAssetHappyPath(testManager, root, root);
	}
	SECTION("Cancel asset request")
	{
		auto manageAssetHelper = ManageAssetHelper(testManager);
		SECTION("Invalid ID")
		{
			manageAssetHelper.applyManageAssetTx(root, 0, manageAssetHelper.createCancelRequest(), ManageAssetResultCode::REQUEST_NOT_FOUND);
		}
		SECTION("Request not found")
		{
			manageAssetHelper.applyManageAssetTx(root, 12, manageAssetHelper.createCancelRequest(), ManageAssetResultCode::REQUEST_NOT_FOUND);
		}
		SECTION("Request has invalid type")
		{
			// 1. create asset
			// 2. create pre issuance request for it
			// 3. try to cancel it with asset request
			AssetCode asset = "USDT";
			manageAssetHelper.createAsset(root, root.key, asset, root);
			auto issuanceHelper = IssuanceRequestHelper(testManager);
			auto requestResult = issuanceHelper.applyCreatePreIssuanceRequest(root, root.key, asset, 10000, SecretKey::random().getStrKeyPublic());
			auto cancelRequest = manageAssetHelper.createCancelRequest();
			manageAssetHelper.applyManageAssetTx(root, requestResult.success().requestID, cancelRequest, ManageAssetResultCode::REQUEST_NOT_FOUND);
		}
	}
}
