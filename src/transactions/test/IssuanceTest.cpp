// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0
#include <transactions/test/test_helper/ManageAssetTestHelper.h>
#include "main/Config.h"
#include "overlay/LoopbackPeer.h"
#include "main/test.h"
#include "ledger/AssetHelper.h"
#include "ledger/BalanceHelper.h"
#include "ledger/ReviewableRequestHelper.h"
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

	auto balanceHelper = BalanceHelper::Instance();
	auto newAccountBalance = balanceHelper->loadBalance(newAccountKP.getPublicKey(), assetCode, testManager->getDB(), nullptr);
	REQUIRE(newAccountBalance);

	auto reviewableRequestHelper = ReviewableRequestHelper::Instance();

	SECTION("Auto review of issuance request")
	{
		auto issuanceRequestResult = issuanceRequestHelper.applyCreateIssuanceRequest(assetOwner, assetCode, preIssuedAmount,
                                                                                      newAccountBalance->getBalanceID(),
                                                                                      newAccountKP.getStrKeyPublic());
		REQUIRE(issuanceRequestResult.success().fulfilled);
		auto issuanceRequest = reviewableRequestHelper->loadRequest(issuanceRequestResult.success().requestID, testManager->getDB());
		REQUIRE(!issuanceRequest);
	}
	SECTION("Request was created but was not auto reviwed")
	{
		// request was create but not fulfilleds as amount of pre issued asset is insufficient
		uint64_t issuanceRequestAmount = preIssuedAmount + 1;
		auto issuanceRequestResult = issuanceRequestHelper.applyCreateIssuanceRequest(assetOwner, assetCode,
			issuanceRequestAmount, newAccountBalance->getBalanceID(), newAccountKP.getStrKeyPublic());
		REQUIRE(!issuanceRequestResult.success().fulfilled);
		auto newAccountBalanceAfterRequest = balanceHelper->loadBalance(newAccountBalance->getBalanceID(), testManager->getDB());
		REQUIRE(newAccountBalanceAfterRequest->getAmount() == 0);

		// try to review request
		auto reviewIssuanceRequestHelper = ReviewIssuanceRequestHelper(testManager);
		reviewIssuanceRequestHelper.applyReviewRequestTx(assetOwner, issuanceRequestResult.success().requestID,
			ReviewRequestOpAction::APPROVE, "", ReviewRequestResultCode::INSUFFICIENT_AVAILABLE_FOR_ISSUANCE_AMOUNT);

		// authorized more asset to be issued & review request
		issuanceRequestHelper.authorizePreIssuedAmount(assetOwner, assetOwner.key, assetCode, issuanceRequestAmount, root);

		// try review request
		reviewIssuanceRequestHelper.applyReviewRequestTx(assetOwner, issuanceRequestResult.success().requestID,
			ReviewRequestOpAction::APPROVE, "");

		// check state after request approval
		newAccountBalanceAfterRequest = balanceHelper->loadBalance(newAccountBalance->getBalanceID(), testManager->getDB());
		REQUIRE(newAccountBalanceAfterRequest->getAmount() == issuanceRequestAmount);

		auto assetHelper = AssetHelper::Instance();
		auto assetFrame = assetHelper->loadAsset(assetCode, testManager->getDB());
		REQUIRE(assetFrame->getIssued() == issuanceRequestAmount);
		REQUIRE(assetFrame->getAvailableForIssuance() == preIssuedAmount);
	}

}

void createPreIssuanceRequestHardPath(TestManager::pointer testManager, Account &assetOwner, Account &root)
{
    ManageAssetTestHelper manageAssetTestHelper = ManageAssetTestHelper(testManager);
    IssuanceRequestHelper issuanceRequestHelper = IssuanceRequestHelper(testManager);

    //create one base asset
    AssetCode assetCode = "UAH";
    uint64_t maxIssuanceAmount = UINT64_MAX/2;
    SecretKey preissuedSigner = SecretKey::random();
    uint32 baseAssetPolicy = static_cast<uint32>(AssetPolicy::BASE_ASSET);
    auto assetCreationRequest = manageAssetTestHelper.createAssetCreationRequest(assetCode, "UAH", preissuedSigner.getPublicKey(),
                                                                                 "long description", "http://bank.gov.ua",
                                                                                 maxIssuanceAmount, baseAssetPolicy, "123");
    manageAssetTestHelper.applyManageAssetTx(root, 0, assetCreationRequest);

    uint64 amount = 10000;
    std::string reference = SecretKey::random().getStrKeyPublic();

    SECTION("asset code malformed")
    {
        AssetCode invailAssetCode = "0X!";
        issuanceRequestHelper.applyCreatePreIssuanceRequest(root, preissuedSigner, invailAssetCode, amount, reference,
                                                            CreatePreIssuanceRequestResultCode::ASSET_NOT_FOUND);
    }

    SECTION("try to pre-issue zero amount")
    {
        issuanceRequestHelper.applyCreatePreIssuanceRequest(root, preissuedSigner, assetCode, 0, reference,
                                                            CreatePreIssuanceRequestResultCode::INVALID_AMOUNT);
    }

    SECTION("try to create request with empty reference")
    {
        issuanceRequestHelper.applyCreatePreIssuanceRequest(root, preissuedSigner, assetCode, amount, "",
                                                            CreatePreIssuanceRequestResultCode::INVALID_REFERENCE);
    }

    SECTION("preissuance requests duplication")
    {
        //first request
        issuanceRequestHelper.applyCreatePreIssuanceRequest(root, preissuedSigner, assetCode, amount, reference);

        //try create request with the same reference
        issuanceRequestHelper.applyCreatePreIssuanceRequest(root, preissuedSigner, assetCode, amount, reference,
                                                            CreatePreIssuanceRequestResultCode::REFERENCE_DUPLICATION);
    }

    SECTION("try pre-issue non-existing asset")
    {
        AssetCode nonExistingAsset = "AAA";
        issuanceRequestHelper.applyCreatePreIssuanceRequest(root, preissuedSigner, nonExistingAsset, amount, reference,
                                                            CreatePreIssuanceRequestResultCode::ASSET_NOT_FOUND);
    }

    SECTION("try to pre-issue not my asset")
    {
        //create one more account
        SecretKey syndicate = SecretKey::random();
        Account syndicateAccount = Account{syndicate, Salt(0)};
        applyCreateAccountTx(testManager->getApp(), root.key, syndicate, 0, AccountType::SYNDICATE);

        //root is asset owner, syndicate tries to preissue some amount of asset
        issuanceRequestHelper.applyCreatePreIssuanceRequest(syndicateAccount, preissuedSigner, assetCode, amount, reference,
                                                            CreatePreIssuanceRequestResultCode::NOT_AUTHORIZED_UPLOAD);

        //syndicate creates it's own asset
        AssetCode syndicateAsset = "USD";
        manageAssetTestHelper.createAsset(syndicateAccount, preissuedSigner, syndicateAsset, root);

        //master tries to preissue it
        issuanceRequestHelper.applyCreatePreIssuanceRequest(root, preissuedSigner, syndicateAsset, amount, reference,
                                                            CreatePreIssuanceRequestResultCode::NOT_AUTHORIZED_UPLOAD);
    }

    SECTION("try to pre-issue without pre-issued asset signer's signature")
    {
        SecretKey adversary = SecretKey::random();
        issuanceRequestHelper.applyCreatePreIssuanceRequest(root, adversary, assetCode, amount, reference,
                                                            CreatePreIssuanceRequestResultCode::INVALID_SIGNATURE);
    }

    SECTION("exceed maximum issuance amount")
    {
        uint64_t bigAmount = maxIssuanceAmount - 1;

        SECTION("successful pre-issuance")
        {
            issuanceRequestHelper.applyCreatePreIssuanceRequest(root, preissuedSigner, assetCode, bigAmount, reference);
        }

        SECTION("exceed max issuance amount of asset")
        {
            uint64_t bigEnoughAmount = bigAmount + 2;
            issuanceRequestHelper.applyCreatePreIssuanceRequest(root, preissuedSigner, assetCode, bigEnoughAmount, reference,
                                                                CreatePreIssuanceRequestResultCode::EXCEEDED_MAX_AMOUNT);
        }

        SECTION("exceed by overflowing asset's available for issuance amount")
        {
            //pre-issue some amount first
            issuanceRequestHelper.authorizePreIssuedAmount(root, preissuedSigner, assetCode, bigAmount, root);

            //overflow
            uint64_t bigEnoughAmount = 2 * bigAmount;
            issuanceRequestHelper.applyCreatePreIssuanceRequest(root, preissuedSigner, assetCode, bigEnoughAmount, reference,
                                                                CreatePreIssuanceRequestResultCode::EXCEEDED_MAX_AMOUNT);
        }

        SECTION("exceed by overflowing asset's issued amount")
        {
            //issue some amount first
            SecretKey receiver = SecretKey::random();
            applyCreateAccountTx(testManager->getApp(), root.key, receiver, 0, AccountType::GENERAL);
            auto balanceHelper = BalanceHelper::Instance();
            auto receiverBalance = balanceHelper->loadBalance(receiver.getPublicKey(), assetCode, testManager->getDB(), nullptr);
            REQUIRE(receiverBalance);

            issuanceRequestHelper.authorizePreIssuedAmount(root, preissuedSigner, assetCode, bigAmount, root);
            issuanceRequestHelper.applyCreateIssuanceRequest(root, assetCode, bigAmount, receiverBalance->getBalanceID(), reference);

            //try to pre-issue amount that will overflow asset's issued amount
            uint64_t bigEnoughAmount = 2 * bigAmount;
            std::string newReference = SecretKey::random().getStrKeyPublic();
            issuanceRequestHelper.applyCreatePreIssuanceRequest(root, preissuedSigner, assetCode, bigEnoughAmount, newReference,
                                                                CreatePreIssuanceRequestResultCode::EXCEEDED_MAX_AMOUNT);
        }
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

    SECTION("PreIssuance hard path")
    {
        createPreIssuanceRequestHardPath(testManager, root, root);
    }

}
