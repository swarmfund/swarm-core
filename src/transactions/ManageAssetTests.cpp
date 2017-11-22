// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0
#include "main/Config.h"
#include "overlay/LoopbackPeer.h"
#include "main/test.h"
#include "lib/catch.hpp"
#include "TxTests.h"
#include "transactions/test_helper/TestManager.h"
#include "transactions/test_helper/ManageAssetHelper.h"
#include "transactions/test_helper/ReviewAssetRequestHelper.h"

using namespace stellar;
using namespace stellar::txtest;

void testManageAssetHappyPath(TestManager::pointer testManager, Account& account, Account& root) {
	SECTION("Can create asset")
	{
		auto preissuedSigner = SecretKey::random();
		auto manageAssetHelper = ManageAssetHelper(testManager);
		AssetCode assetCode = "USD";
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

	// TODO add better coverage
	SECTION("Root happy path")
	{
		testManageAssetHappyPath(testManager, root, root);
	}
}
