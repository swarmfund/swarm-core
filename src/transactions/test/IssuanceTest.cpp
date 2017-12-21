// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0
#include <transactions/test/test_helper/ManageAssetTestHelper.h>
#include <transactions/test/test_helper/CreateAccountTestHelper.h>
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
    CreateAccountTestHelper createAccountTestHelper(testManager);
    createAccountTestHelper.applyCreateAccountTx(root, newAccountKP.getPublicKey(), AccountType::GENERAL);

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
    ManageAssetTestHelper manageAssetTestHelper(testManager);
    IssuanceRequestHelper issuanceRequestHelper(testManager);
    CreateAccountTestHelper createAccountTestHelper(testManager);

    //create one base asset
    AssetCode assetCode = "UAH";
    uint64_t maxIssuanceAmount = UINT64_MAX/2;
    SecretKey preissuedSigner = SecretKey::random();
    uint32 baseAssetPolicy = static_cast<uint32>(AssetPolicy::BASE_ASSET);
    auto assetCreationRequest = manageAssetTestHelper.createAssetCreationRequest(assetCode, "UAH", preissuedSigner.getPublicKey(),
                                                                                 "long description", "http://bank.gov.ua",
                                                                                 maxIssuanceAmount, baseAssetPolicy, "123");
    manageAssetTestHelper.applyManageAssetTx(assetOwner, 0, assetCreationRequest);

    uint64 amount = 10000;
    std::string reference = SecretKey::random().getStrKeyPublic();

    SECTION("asset code malformed")
    {
        AssetCode invailAssetCode = "0X!";
        issuanceRequestHelper.applyCreatePreIssuanceRequest(assetOwner, preissuedSigner, invailAssetCode, amount, reference,
                                                            CreatePreIssuanceRequestResultCode::ASSET_NOT_FOUND);
    }

    SECTION("try to pre-issue zero amount")
    {
        issuanceRequestHelper.applyCreatePreIssuanceRequest(assetOwner, preissuedSigner, assetCode, 0, reference,
                                                            CreatePreIssuanceRequestResultCode::INVALID_AMOUNT);
    }

    SECTION("try to create request with empty reference")
    {
        issuanceRequestHelper.applyCreatePreIssuanceRequest(assetOwner, preissuedSigner, assetCode, amount, "",
                                                            CreatePreIssuanceRequestResultCode::INVALID_REFERENCE);
    }

    SECTION("preissuance requests duplication")
    {
        //first request
        issuanceRequestHelper.applyCreatePreIssuanceRequest(assetOwner, preissuedSigner, assetCode, amount, reference);

        //try create request with the same reference
        issuanceRequestHelper.applyCreatePreIssuanceRequest(assetOwner, preissuedSigner, assetCode, amount, reference,
                                                            CreatePreIssuanceRequestResultCode::REFERENCE_DUPLICATION);
    }

    SECTION("try pre-issue non-existing asset")
    {
        AssetCode nonExistingAsset = "AAA";
        issuanceRequestHelper.applyCreatePreIssuanceRequest(assetOwner, preissuedSigner, nonExistingAsset, amount, reference,
                                                            CreatePreIssuanceRequestResultCode::ASSET_NOT_FOUND);
    }

    SECTION("try to pre-issue not my asset")
    {
        //create one more account
        SecretKey syndicate = SecretKey::random();
        Account syndicateAccount = Account{syndicate, Salt(0)};
        createAccountTestHelper.applyCreateAccountTx(root, syndicate.getPublicKey(), AccountType::SYNDICATE);

        //root is asset owner, syndicate tries to preissue some amount of asset
        issuanceRequestHelper.applyCreatePreIssuanceRequest(syndicateAccount, preissuedSigner, assetCode, amount, reference,
                                                            CreatePreIssuanceRequestResultCode::NOT_AUTHORIZED_UPLOAD);

        //syndicate creates it's own asset
        AssetCode syndicateAsset = "USD";
        manageAssetTestHelper.createAsset(syndicateAccount, preissuedSigner, syndicateAsset, root, 0);

        //master tries to preissue it
        issuanceRequestHelper.applyCreatePreIssuanceRequest(root, preissuedSigner, syndicateAsset, amount, reference,
                                                            CreatePreIssuanceRequestResultCode::NOT_AUTHORIZED_UPLOAD);
    }

    SECTION("try to pre-issue without pre-issued asset signer's signature")
    {
        SecretKey adversary = SecretKey::random();
        issuanceRequestHelper.applyCreatePreIssuanceRequest(assetOwner, adversary, assetCode, amount, reference,
                                                            CreatePreIssuanceRequestResultCode::INVALID_SIGNATURE);
    }

    SECTION("exceed maximum issuance amount")
    {
        uint64_t bigAmount = maxIssuanceAmount - 1;

        SECTION("successful pre-issuance")
        {
            issuanceRequestHelper.applyCreatePreIssuanceRequest(assetOwner, preissuedSigner, assetCode, bigAmount, reference);
        }

        SECTION("exceed max issuance amount of asset")
        {
            uint64_t bigEnoughAmount = bigAmount + 2;
            issuanceRequestHelper.applyCreatePreIssuanceRequest(assetOwner, preissuedSigner, assetCode, bigEnoughAmount, reference,
                                                                CreatePreIssuanceRequestResultCode::EXCEEDED_MAX_AMOUNT);
        }

        SECTION("exceed by overflowing asset's available for issuance amount")
        {
            //pre-issue some amount first
            issuanceRequestHelper.authorizePreIssuedAmount(assetOwner, preissuedSigner, assetCode, bigAmount, root);

            //overflow
            uint64_t bigEnoughAmount = 2 * bigAmount;
            issuanceRequestHelper.applyCreatePreIssuanceRequest(assetOwner, preissuedSigner, assetCode, bigEnoughAmount, reference,
                                                                CreatePreIssuanceRequestResultCode::EXCEEDED_MAX_AMOUNT);
        }

        SECTION("exceed by overflowing asset's issued amount")
        {
            //issue some amount first
            SecretKey receiver = SecretKey::random();
            createAccountTestHelper.applyCreateAccountTx(root, receiver.getPublicKey(), AccountType::GENERAL);
            auto balanceHelper = BalanceHelper::Instance();
            auto receiverBalance = balanceHelper->loadBalance(receiver.getPublicKey(), assetCode, testManager->getDB(), nullptr);
            REQUIRE(receiverBalance);

            issuanceRequestHelper.authorizePreIssuedAmount(assetOwner, preissuedSigner, assetCode, bigAmount, root);
            issuanceRequestHelper.applyCreateIssuanceRequest(assetOwner, assetCode, bigAmount, receiverBalance->getBalanceID(), reference);

            //try to pre-issue amount that will overflow asset's issued amount
            uint64_t bigEnoughAmount = 2 * bigAmount;
            std::string newReference = SecretKey::random().getStrKeyPublic();
            issuanceRequestHelper.applyCreatePreIssuanceRequest(root, preissuedSigner, assetCode, bigEnoughAmount, newReference,
                                                                CreatePreIssuanceRequestResultCode::EXCEEDED_MAX_AMOUNT);
        }
    }
}

void createIssuanceRequestHardPath(TestManager::pointer testManager, Account &assetOwner, Account &root)
{
    ManageAssetTestHelper manageAssetTestHelper = ManageAssetTestHelper(testManager);
    IssuanceRequestHelper issuanceRequestHelper = IssuanceRequestHelper(testManager);
    CreateAccountTestHelper createAccountTestHelper(testManager);

    //create one base asset
    AssetCode assetCode = "UAH";
    uint64_t maxIssuanceAmount = UINT64_MAX/2;
    SecretKey preissuedSigner = SecretKey::random();
    uint32 baseAssetPolicy = static_cast<uint32>(AssetPolicy::BASE_ASSET);
    auto assetCreationRequest = manageAssetTestHelper.createAssetCreationRequest(assetCode, "UAH", preissuedSigner.getPublicKey(),
                                                                                 "long description", "http://bank.gov.ua",
                                                                                 maxIssuanceAmount, baseAssetPolicy, "123");
    manageAssetTestHelper.applyManageAssetTx(assetOwner, 0, assetCreationRequest);

    //pre-issue some amount
    issuanceRequestHelper.authorizePreIssuedAmount(assetOwner, preissuedSigner, assetCode, maxIssuanceAmount - 1, root);

    //create receiver account
    SecretKey receiverKP = SecretKey::random();
    createAccountTestHelper.applyCreateAccountTx(root, receiverKP.getPublicKey(), AccountType::GENERAL);
    auto balanceHelper = BalanceHelper::Instance();
    auto receiverBalance = balanceHelper->loadBalance(receiverKP.getPublicKey(), assetCode, testManager->getDB(),
                                                      nullptr);
    REQUIRE(receiverBalance);

    //declare here for convenience
    std::string reference = SecretKey::random().getStrKeyPublic();
    uint64 amount = 10000;

    SECTION("invalid asset code")
    {
        AssetCode invalidAssetCode = "U0H";
        issuanceRequestHelper.applyCreateIssuanceRequest(assetOwner, invalidAssetCode, amount, receiverBalance->getBalanceID(),
                                                         reference, CreateIssuanceRequestResultCode::ASSET_NOT_FOUND);
    }

    SECTION("try to issue zero amount")
    {
        issuanceRequestHelper.applyCreateIssuanceRequest(assetOwner, assetCode, 0, receiverBalance->getBalanceID(), reference,
                                                         CreateIssuanceRequestResultCode::INVALID_AMOUNT);
    }

    SECTION("empty reference")
    {
        issuanceRequestHelper.applyCreateIssuanceRequest(assetOwner, assetCode, amount, receiverBalance->getBalanceID(), "",
                                                         CreateIssuanceRequestResultCode::REFERENCE_DUPLICATION);
    }

    SECTION("try to use the same reference twice")
    {
        uint64_t issuanceAmount = amount/4;
        issuanceRequestHelper.applyCreateIssuanceRequest(assetOwner, assetCode, issuanceAmount, receiverBalance->getBalanceID(),
                                                         reference);

        //try to issue issuanceAmount again using the same reference
        issuanceRequestHelper.applyCreateIssuanceRequest(assetOwner, assetCode, issuanceAmount, receiverBalance->getBalanceID(),
                                                         reference, CreateIssuanceRequestResultCode::REFERENCE_DUPLICATION);
    }

    SECTION("try to issue non-existing asset")
    {
        AssetCode nonExistentAsset = "CCC";
        issuanceRequestHelper.applyCreateIssuanceRequest(assetOwner, nonExistentAsset, amount, receiverBalance->getBalanceID(),
                                                         reference, CreateIssuanceRequestResultCode::ASSET_NOT_FOUND);
    }

    SECTION("try to issue not my asset")
    {
        //create syndicate account
        SecretKey syndicateKP = SecretKey::random();
        Account syndicate = Account{syndicateKP, Salt(0)};
        createAccountTestHelper.applyCreateAccountTx(root, syndicateKP.getPublicKey(), AccountType::SYNDICATE);

        //try to issue some amount from syndicate account
        issuanceRequestHelper.applyCreateIssuanceRequest(syndicate, assetCode, amount, receiverBalance->getBalanceID(),
                                                         reference, CreateIssuanceRequestResultCode::NOT_AUTHORIZED);
    }

    SECTION("exceed max issuance amount")
    {
        issuanceRequestHelper.applyCreateIssuanceRequest(assetOwner, assetCode, maxIssuanceAmount + 1, receiverBalance->getBalanceID(),
                                                         reference, CreateIssuanceRequestResultCode::EXCEEDS_MAX_ISSUANCE_AMOUNT);
    }

    SECTION("try to issue to non-existing receiver")
    {
        BalanceID nonExistingReceiver = SecretKey::random().getPublicKey();
        issuanceRequestHelper.applyCreateIssuanceRequest(assetOwner, assetCode, amount, nonExistingReceiver, reference,
                                                         CreateIssuanceRequestResultCode::NO_COUNTERPARTY);
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

    SECTION("create pre-issuance request hard path")
    {
        createPreIssuanceRequestHardPath(testManager, root, root);
    }

    SECTION("create issuance request hard path")
    {
        createIssuanceRequestHardPath(testManager, root, root);
    }

}
