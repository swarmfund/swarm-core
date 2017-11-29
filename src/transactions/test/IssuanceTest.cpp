// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0
#include "main/Config.h"
#include "overlay/LoopbackPeer.h"
#include "main/test.h"
#include "lib/catch.hpp"
#include "TxTests.h"
#include "test_helper/TestManager.h"
#include "test_helper/IssuanceRequestHelper.h"
#include "test_helper/ReviewIssuanceRequestHelper.h"
#include "test_helper/ReviewPreIssuanceRequestHelper.h"


using namespace stellar;
using namespace stellar::txtest;

typedef std::unique_ptr<Application> appPtr;

void createIssuanceRequestHappyPath(TestManager::pointer testManager, Account& assetOwner, Account& root) {
	auto issuanceRequestHelper = IssuanceRequestHelper(testManager);
	AssetCode assetCode = "EUR";
	uint64_t preIssuedAmount = 10000;
	issuanceRequestHelper.createAssetWithPreIssuedAmount(assetOwner, assetCode, preIssuedAmount, root);
	// create new account with balance 
	auto newAccountKP = SecretKey::random();
	applyCreateAccountTx(testManager->getApp(), root.key, newAccountKP, root.getNextSalt(), AccountType::GENERAL);
	auto newAccountBalance = BalanceFrame::loadBalance(newAccountKP.getPublicKey(), assetCode, testManager->getDB(), nullptr);
	REQUIRE(newAccountBalance);

	SECTION("Auto review of issuance request")
	{
		auto issuanceRequestResult = issuanceRequestHelper.applyCreateIssuanceRequest(assetOwner, assetCode, preIssuedAmount, newAccountBalance->getBalanceID(), newAccountKP.getStrKeyPublic());
		REQUIRE(issuanceRequestResult.success().fulfilled);
		auto issuanceRequest = ReviewableRequestFrame::loadRequest(issuanceRequestResult.success().requestID, testManager->getDB());
		REQUIRE(!issuanceRequest);
	}
	SECTION("Request was created but was not auto reviwed")
	{
		// request was create but not fulfilleds as amount of pre issued asset is insufficient
		uint64_t issuanceRequestAmount = preIssuedAmount + 1;
		auto issuanceRequestResult = issuanceRequestHelper.applyCreateIssuanceRequest(assetOwner, assetCode,
			issuanceRequestAmount, newAccountBalance->getBalanceID(), newAccountKP.getStrKeyPublic());
		REQUIRE(!issuanceRequestResult.success().fulfilled);
		auto newAccountBalanceAfterRequest = BalanceFrame::loadBalance(newAccountBalance->getBalanceID(), testManager->getDB());
		REQUIRE(newAccountBalanceAfterRequest->getAmount() == 0);

		// try to review request
		auto reviewIssuanceRequestHelper = ReviewIssuanceRequestHelper(testManager);
		reviewIssuanceRequestHelper.applyReviewRequestTx(assetOwner, issuanceRequestResult.success().requestID,
			ReviewRequestOpAction::APPROVE, "", ReviewRequestResultCode::INSUFFICIENT_AVAILABLE_FOR_ISSUANCE_AMOUNT);

		// authorized more asset to be issued & review request
		issuanceRequestHelper.authorizePreIssuedAmount(assetOwner, assetOwner, assetCode, issuanceRequestAmount, root);

		// try review request
		reviewIssuanceRequestHelper.applyReviewRequestTx(assetOwner, issuanceRequestResult.success().requestID,
			ReviewRequestOpAction::APPROVE, "");

		// check state after request approval
		newAccountBalanceAfterRequest = BalanceFrame::loadBalance(newAccountBalance->getBalanceID(), testManager->getDB());
		REQUIRE(newAccountBalanceAfterRequest->getAmount() == issuanceRequestAmount);
		auto assetFrame = AssetFrame::loadAsset(assetCode, testManager->getDB());
		REQUIRE(assetFrame->getIssued() == issuanceRequestAmount);
		REQUIRE(assetFrame->getAvailableForIssuance() == preIssuedAmount);
	}

}

TEST_CASE("Issuance", "[tx][issuance]")
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
		createIssuanceRequestHappyPath(testManager, root, root);
	}
}
