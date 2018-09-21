// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0
#include <transactions/test/test_helper/ManageAssetTestHelper.h>
#include <transactions/test/test_helper/CreateAccountTestHelper.h>
#include <ledger/FeeHelper.h>
#include <ledger/AccountHelper.h>
#include <transactions/test/test_helper/ManageKeyValueTestHelper.h>
#include "overlay/LoopbackPeer.h"
#include "main/test.h"
#include "ledger/AssetHelper.h"
#include "ledger/BalanceHelper.h"
#include "ledger/ReviewableRequestHelper.h"
#include "TxTests.h"
#include "test_helper/IssuanceRequestHelper.h"
#include "test_helper/ReviewIssuanceRequestHelper.h"
#include "test_helper/ReviewPreIssuanceRequestHelper.h"
#include "test/test_marshaler.h"
#include "transactions/issuance/CreateIssuanceRequestOpFrame.h"
#include <transactions/test/test_helper/ManageLimitsTestHelper.h>

using namespace std;
using namespace stellar;
using namespace stellar::txtest;

typedef std::unique_ptr<Application> appPtr;

void createIssuanceRequestHappyPath(TestManager::pointer testManager, Account& assetOwner, Account& root) {
	auto issuanceRequestHelper = IssuanceRequestHelper(testManager);
	AssetCode assetToBeIssued = "EUR";
	uint64_t preIssuedAmount = 10000;
	issuanceRequestHelper.createAssetWithPreIssuedAmount(assetOwner, assetToBeIssued, preIssuedAmount, root);
	// create new account with balance 
	auto receiverKP = SecretKey::random();
    CreateAccountTestHelper createAccountTestHelper(testManager);
    createAccountTestHelper.applyCreateAccountTx(root, receiverKP.getPublicKey(), AccountType::GENERAL);

	auto balanceHelper = BalanceHelper::Instance();
	auto receiverBalance = balanceHelper->loadBalance(receiverKP.getPublicKey(), assetToBeIssued, testManager->getDB(), nullptr);
	REQUIRE(receiverBalance);

	auto reviewableRequestHelper = ReviewableRequestHelper::Instance();

    ManageKeyValueTestHelper manageKeyValueHelper(testManager);
    longstring key = ManageKeyValueOpFrame::makeIssuanceTasksKey(assetToBeIssued);
    manageKeyValueHelper.setKey(key)->setUi32Value(0);
    manageKeyValueHelper.doApply(testManager->getApp(), ManageKVAction::PUT, true);

    uint32_t issuanceTasks = 0;

	SECTION("Auto review of issuance request")
	{
		auto issuanceRequestResult = issuanceRequestHelper.applyCreateIssuanceRequest(assetOwner, assetToBeIssued, preIssuedAmount,
                                                                                      receiverBalance->getBalanceID(),
                                                                                      receiverKP.getStrKeyPublic(), &issuanceTasks,
                                                                                      CreateIssuanceRequestResultCode::SUCCESS,
                                                                                      "{}");
		REQUIRE(issuanceRequestResult.success().fulfilled);
		auto issuanceRequest = reviewableRequestHelper->loadRequest(issuanceRequestResult.success().requestID, testManager->getDB());
		REQUIRE(!issuanceRequest);
	}
    SECTION("Auto review is disabled")
	{
	    //8 is a magic value - first bit not reserved for system tasks
        uint32_t allTasks = 8;
        auto issuanceRequestResult = issuanceRequestHelper.applyCreateIssuanceRequest(assetOwner, assetToBeIssued, preIssuedAmount,
            receiverBalance->getBalanceID(),
            receiverKP.getStrKeyPublic(), &allTasks, CreateIssuanceRequestResultCode::SUCCESS, "{}");
        REQUIRE(!issuanceRequestResult.success().fulfilled);
	}
    
    SECTION("charge fee on issuance") 
    {
        //set ISSUANCE_FEE for newAccount
        uint64_t percentFee = 1 * ONE;
        uint64_t fixedFee = preIssuedAmount/2;
        AccountID account = receiverKP.getPublicKey();
        auto feeEntry = createFeeEntry(FeeType::ISSUANCE_FEE, fixedFee, percentFee, assetToBeIssued, &account, nullptr);
        applySetFees(testManager->getApp(), root.key, root.getNextSalt(), &feeEntry, false);

        auto accountFrame = AccountHelper::Instance()->loadAccount(account, testManager->getDB());
        auto feeFrame = FeeHelper::Instance()->loadForAccount(FeeType::ISSUANCE_FEE, assetToBeIssued, FeeFrame::SUBTYPE_ANY,
                                                              accountFrame, preIssuedAmount, testManager->getDB());
        REQUIRE(feeFrame);

        SECTION("successful issuance")
        {
            auto issuanceRequestResult = issuanceRequestHelper.applyCreateIssuanceRequest(assetOwner, assetToBeIssued, preIssuedAmount,
                                                                                          receiverBalance->getBalanceID(), receiverKP.getStrKeyPublic());
            uint64_t percentFeeToPay = 0;
            feeFrame->calculatePercentFee(preIssuedAmount, percentFeeToPay, ROUND_UP);

            REQUIRE(issuanceRequestResult.success().fee.fixed == fixedFee);
            REQUIRE(issuanceRequestResult.success().fee.percent == percentFeeToPay);
        }

        SECTION("total fee exceeds issuance amount")
        {
            uint64_t insufficientAmount = fixedFee;
            auto result = issuanceRequestHelper.applyCreateIssuanceRequest(assetOwner, assetToBeIssued,
                                                                           insufficientAmount,
                                                                           receiverBalance->getBalanceID(),
                                                                           receiverKP.getStrKeyPublic(),
                                                                           &issuanceTasks,
                                                                           CreateIssuanceRequestResultCode::FEE_EXCEEDS_AMOUNT);
        }

    }
    
	SECTION("Request was created but was not auto reviewed")
	{
		// request was create but not fulfilleds as amount of pre issued asset is insufficient
		uint64_t issuanceRequestAmount = preIssuedAmount + 1;
		auto issuanceRequestResult = issuanceRequestHelper.applyCreateIssuanceRequest(assetOwner, assetToBeIssued,
			issuanceRequestAmount, receiverBalance->getBalanceID(), receiverKP.getStrKeyPublic());
		REQUIRE(!issuanceRequestResult.success().fulfilled);
		auto newAccountBalanceAfterRequest = balanceHelper->loadBalance(receiverBalance->getBalanceID(), testManager->getDB());
		REQUIRE(newAccountBalanceAfterRequest->getAmount() == 0);

		// try to review request
		auto reviewIssuanceRequestHelper = ReviewIssuanceRequestHelper(testManager);
		reviewIssuanceRequestHelper.applyReviewRequestTx(assetOwner, issuanceRequestResult.success().requestID,
			ReviewRequestOpAction::APPROVE, "");

		// authorized more asset to be issued & review request
		issuanceRequestHelper.authorizePreIssuedAmount(assetOwner, assetOwner.key, assetToBeIssued, issuanceRequestAmount, root);

		// try review request
		reviewIssuanceRequestHelper.applyReviewRequestTx(assetOwner, issuanceRequestResult.success().requestID,
			ReviewRequestOpAction::APPROVE, "");

		// check state after request approval
		newAccountBalanceAfterRequest = balanceHelper->loadBalance(receiverBalance->getBalanceID(), testManager->getDB());
		REQUIRE(newAccountBalanceAfterRequest->getAmount() == issuanceRequestAmount);

		auto assetHelper = AssetHelper::Instance();
		auto assetFrame = assetHelper->loadAsset(assetToBeIssued, testManager->getDB());
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
    auto assetCreationRequest = manageAssetTestHelper.createAssetCreationRequest(assetCode, preissuedSigner.getPublicKey(),
                                                                                 "{}", maxIssuanceAmount, baseAssetPolicy);
    manageAssetTestHelper.applyManageAssetTx(assetOwner, 0, assetCreationRequest);

    uint64 amount = 10000;
    std::string reference = SecretKey::random().getStrKeyPublic();

    ManageKeyValueTestHelper manageKeyValueHelper(testManager);
    longstring key = ManageKeyValueOpFrame::makeIssuanceTasksKey(assetCode);
    manageKeyValueHelper.setKey(key)->setUi32Value(0);
    manageKeyValueHelper.doApply(testManager->getApp(), ManageKVAction::PUT, true);

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

    uint32_t issuanceTasks = 0;

    //create one base asset
    AssetCode assetCode = "UAH";
    uint64_t maxIssuanceAmount = UINT64_MAX/2;
    SecretKey preissuedSigner = SecretKey::random();
    uint32 baseAssetPolicy = static_cast<uint32>(AssetPolicy::BASE_ASSET);
    uint32 requiresKYCPolicy = static_cast<uint32>(AssetPolicy::REQUIRES_KYC);
    auto requiresVerificationPolicy = static_cast<uint32>(AssetPolicy::REQUIRES_VERIFICATION);
    auto assetCreationRequest = manageAssetTestHelper.createAssetCreationRequest(assetCode, preissuedSigner.getPublicKey(),
                                                                                 "{}", maxIssuanceAmount,
                                                                                 baseAssetPolicy | requiresKYCPolicy |
                                                                                 requiresVerificationPolicy);
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

    SECTION("Issuance tasks don't exist")
    {
        issuanceRequestHelper.applyCreateIssuanceRequest(assetOwner, assetCode, amount, receiverBalance->getBalanceID(),
                                                         reference, nullptr,
                                                         CreateIssuanceRequestResultCode::ISSUANCE_TASKS_NOT_FOUND);
    }

    ManageKeyValueTestHelper manageKeyValueHelper(testManager);
    longstring key = ManageKeyValueOpFrame::makeIssuanceTasksKey(assetCode);
    manageKeyValueHelper.setKey(key)->setUi32Value(0);
    manageKeyValueHelper.doApply(testManager->getApp(), ManageKVAction::PUT, true);


    SECTION("invalid asset code")
    {
        AssetCode invalidAssetCode = "U0H";
        issuanceRequestHelper.applyCreateIssuanceRequest(assetOwner, invalidAssetCode, amount, receiverBalance->getBalanceID(),
                                                         reference, &issuanceTasks, CreateIssuanceRequestResultCode::ASSET_NOT_FOUND);
    }

    SECTION("try to issue zero amount")
    {
        issuanceRequestHelper.applyCreateIssuanceRequest(assetOwner, assetCode, 0, receiverBalance->getBalanceID(), reference,
                                                         &issuanceTasks, CreateIssuanceRequestResultCode::INVALID_AMOUNT);
    }

    SECTION("empty reference")
    {
        issuanceRequestHelper.applyCreateIssuanceRequest(assetOwner, assetCode, amount, receiverBalance->getBalanceID(), "",
                                                         &issuanceTasks, CreateIssuanceRequestResultCode::REFERENCE_DUPLICATION);
    }

    SECTION("try to use the same reference twice")
    {
        uint64_t issuanceAmount = amount/4;
        issuanceRequestHelper.applyCreateIssuanceRequest(assetOwner, assetCode, issuanceAmount, receiverBalance->getBalanceID(),
                                                         reference);

        //try to issue issuanceAmount again using the same reference
        issuanceRequestHelper.applyCreateIssuanceRequest(assetOwner, assetCode, issuanceAmount, receiverBalance->getBalanceID(),
                                                         reference, &issuanceTasks,
                                                         CreateIssuanceRequestResultCode::REFERENCE_DUPLICATION);
    }

    SECTION("try to issue non-existing asset")
    {
        AssetCode nonExistentAsset = "CCC";
        issuanceRequestHelper.applyCreateIssuanceRequest(assetOwner, nonExistentAsset, amount, receiverBalance->getBalanceID(),
                                                         reference, &issuanceTasks,
                                                         CreateIssuanceRequestResultCode::ASSET_NOT_FOUND);
    }

    SECTION("try to issue not my asset")
    {
        //create syndicate account
        SecretKey syndicateKP = SecretKey::random();
        Account syndicate = Account{syndicateKP, Salt(0)};
        createAccountTestHelper.applyCreateAccountTx(root, syndicateKP.getPublicKey(), AccountType::SYNDICATE);

        //try to issue some amount from syndicate account
        issuanceRequestHelper.applyCreateIssuanceRequest(syndicate, assetCode, amount, receiverBalance->getBalanceID(),
                                                         reference, &issuanceTasks,
                                                         CreateIssuanceRequestResultCode::NOT_AUTHORIZED);
    }

    SECTION("exceed max issuance amount")
    {
        issuanceRequestHelper.applyCreateIssuanceRequest(assetOwner, assetCode, maxIssuanceAmount + 1, receiverBalance->getBalanceID(),
                                                         reference, &issuanceTasks,
                                                         CreateIssuanceRequestResultCode::EXCEEDS_MAX_ISSUANCE_AMOUNT);
    }

    SECTION("try to issue to non-existing receiver")
    {
        BalanceID nonExistingReceiver = SecretKey::random().getPublicKey();
        issuanceRequestHelper.applyCreateIssuanceRequest(assetOwner, assetCode, amount, nonExistingReceiver, reference,
                                                         &issuanceTasks, CreateIssuanceRequestResultCode::NO_COUNTERPARTY);
    }

    SECTION("invalid external details")
    {
        std::string invalidDetails = "{\"key\"}";
        issuanceRequestHelper.applyCreateIssuanceRequest(assetOwner, assetCode, amount, receiverBalance->getBalanceID(),
                                                         reference, &issuanceTasks,
                                                         CreateIssuanceRequestResultCode::INVALID_EXTERNAL_DETAILS,
                                                         invalidDetails);
    }

    SECTION("try to issue asset which requires KYC to not verified")
    {
        auto notVerifiedKP = SecretKey::random();
        AssetCode localAsset = "USD";

        assetCreationRequest = manageAssetTestHelper.createAssetCreationRequest(localAsset, preissuedSigner.getPublicKey(),
                                                                                     "{}", maxIssuanceAmount,
                                                                                     baseAssetPolicy | requiresKYCPolicy);
        manageAssetTestHelper.applyManageAssetTx(assetOwner, 0, assetCreationRequest);

        createAccountTestHelper.applyCreateAccountTx(root, notVerifiedKP.getPublicKey(), AccountType::NOT_VERIFIED);

        auto notVerifiedBalance = balanceHelper->loadBalance(notVerifiedKP.getPublicKey(), localAsset, testManager->getDB(),
                                                          nullptr);

        issuanceRequestHelper.applyCreateIssuanceRequest(assetOwner, localAsset, amount, notVerifiedBalance->getBalanceID(),
                                                         reference, &issuanceTasks,
                                                         CreateIssuanceRequestResultCode::REQUIRES_KYC);

    }

    SECTION("try to issue asset which requires VERIFICATION to not verified")
    {
        auto notVerifiedKP = SecretKey::random();
        createAccountTestHelper.applyCreateAccountTx(root, notVerifiedKP.getPublicKey(), AccountType::NOT_VERIFIED);

        auto notVerifiedBalance = balanceHelper->loadBalance(notVerifiedKP.getPublicKey(), assetCode, testManager->getDB(),
                                                             nullptr);

        issuanceRequestHelper.applyCreateIssuanceRequest(assetOwner, assetCode, amount, notVerifiedBalance->getBalanceID(),
                                                         reference, &issuanceTasks,
                                                         CreateIssuanceRequestResultCode::REQUIRES_VERIFICATION);

    }

    SECTION("try to issue asset which requires KYC to verified")
    {
        auto verifiedKP = SecretKey::random();
        createAccountTestHelper.applyCreateAccountTx(root, verifiedKP.getPublicKey(), AccountType::VERIFIED);

        auto verifiedBalance = balanceHelper->loadBalance(verifiedKP.getPublicKey(), assetCode, testManager->getDB(),
                nullptr);

        issuanceRequestHelper.applyCreateIssuanceRequest(assetOwner, assetCode, amount, verifiedBalance->getBalanceID(),
                                                         reference, &issuanceTasks,
                                                         CreateIssuanceRequestResultCode::REQUIRES_KYC);

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
	TestManager::upgradeToCurrentLedgerVersion(app);

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

    SECTION("Create and review issuance request with tasks")
    {
        auto issuanceRequestHelper = IssuanceRequestHelper(testManager);
        AssetCode assetToBeIssued = "EUR";
        uint64_t preIssuedAmount = 10000;
        auto issuerSecret = SecretKey::random();
        auto issuer = Account{ issuerSecret, Salt(0) };
        CreateAccountTestHelper createAccountTestHelper(testManager);
        createAccountTestHelper.applyCreateAccountTx(root, issuerSecret.getPublicKey(), AccountType::SYNDICATE);
        issuanceRequestHelper.createAssetWithPreIssuedAmount(issuer, assetToBeIssued, preIssuedAmount, root);

        auto& db = testManager->getDB();
        auto issuerBalance = BalanceHelper::Instance()->loadBalance(issuerSecret.getPublicKey(), assetToBeIssued,
                                                                    db, nullptr);
        auto issuerBalanceID = issuerBalance->getBalanceID();

        auto reference = "6146ccf6a66d994f7c363db875e31ca35581450a4bf6d3be6cc9ac79233a69d0";
        uint32_t issuanceTasks = 8;

        SECTION("Create issuance request")
        {
            SECTION("Issuance tasks not found")
            {
                issuanceRequestHelper.applyCreateIssuanceRequest(issuer, assetToBeIssued, preIssuedAmount,
                                                                 issuerBalanceID, reference, nullptr,
                                                                 CreateIssuanceRequestResultCode::ISSUANCE_TASKS_NOT_FOUND);
            }
            SECTION("Trying to set system task")
            {
                uint32_t systemTask = CreateIssuanceRequestOpFrame::ISSUANCE_MANUAL_REVIEW_REQUIRED;
                issuanceRequestHelper.applyCreateIssuanceRequest(issuer, assetToBeIssued, preIssuedAmount,
                                                                 issuerBalanceID, reference, &systemTask,
                                                                 CreateIssuanceRequestResultCode::SYSTEM_TASKS_NOT_ALLOWED);
            }
            SECTION("Insufficient amount to be issued")
            {
                issuanceTasks = 0;
                auto createIssuanceResult =
                        issuanceRequestHelper.applyCreateIssuanceRequest(issuer, assetToBeIssued, preIssuedAmount * 2,
                                                                         issuerBalanceID, reference, &issuanceTasks);

                REQUIRE(!createIssuanceResult.success().fulfilled);

                auto requestID = createIssuanceResult.success().requestID;
                auto request = ReviewableRequestHelper::Instance()->loadRequest(requestID, db);
                REQUIRE(request->getAllTasks() ==
                        CreateIssuanceRequestOpFrame::INSUFFICIENT_AVAILABLE_FOR_ISSUANCE_AMOUNT);
            }
            SECTION("Deposit limits")
            {
                ManageLimitsTestHelper manageLimitsTestHelper(testManager);
                ManageLimitsOp manageLimitsOp;
                manageLimitsOp.details.action(ManageLimitsAction::CREATE);
                manageLimitsOp.details.limitsCreateDetails().accountID.activate() = issuer.key.getPublicKey();
                manageLimitsOp.details.limitsCreateDetails().assetCode = "EUR";
                manageLimitsOp.details.limitsCreateDetails().statsOpType = StatsOpType::DEPOSIT;
                manageLimitsOp.details.limitsCreateDetails().isConvertNeeded = false;

                SECTION("Not exceeded"){
                    manageLimitsOp.details.limitsCreateDetails().dailyOut = preIssuedAmount*3;
                    manageLimitsOp.details.limitsCreateDetails().weeklyOut = preIssuedAmount*6;
                    manageLimitsOp.details.limitsCreateDetails().monthlyOut = preIssuedAmount*12;
                    manageLimitsOp.details.limitsCreateDetails().annualOut = preIssuedAmount*15;
                    manageLimitsTestHelper.applyManageLimitsTx(root, manageLimitsOp);

                    issuanceTasks = 0;
                    auto createIssuanceResult =
                            issuanceRequestHelper.applyCreateIssuanceRequest(issuer, assetToBeIssued, preIssuedAmount/2,
                                                                             issuerBalanceID, reference, &issuanceTasks);
                    REQUIRE(createIssuanceResult.success().fulfilled);

                    auto requestID = createIssuanceResult.success().requestID;
                    auto request = ReviewableRequestHelper::Instance()->loadRequest(requestID, db);
                    REQUIRE(!request);
                }
                SECTION("Exceeded"){
                    manageLimitsOp.details.limitsCreateDetails().dailyOut = preIssuedAmount/3;
                    manageLimitsOp.details.limitsCreateDetails().weeklyOut = preIssuedAmount*6;
                    manageLimitsOp.details.limitsCreateDetails().monthlyOut = preIssuedAmount*12;
                    manageLimitsOp.details.limitsCreateDetails().annualOut = preIssuedAmount*15;
                    manageLimitsTestHelper.applyManageLimitsTx(root, manageLimitsOp);

                    issuanceTasks = 0;
                    auto createIssuanceResult =
                            issuanceRequestHelper.applyCreateIssuanceRequest(issuer, assetToBeIssued, preIssuedAmount/2,
                                                                             issuerBalanceID, reference, &issuanceTasks);
                    REQUIRE(!createIssuanceResult.success().fulfilled);

                    auto requestID = createIssuanceResult.success().requestID;
                    auto request = ReviewableRequestHelper::Instance()->loadRequest(requestID, db);
                    REQUIRE(request->getAllTasks() ==
                            CreateIssuanceRequestOpFrame::DEPOSIT_LIMIT_EXCEEDED);
                }
                SECTION("Limits not exceeded but insufficient amount preissued")
                {
                    manageLimitsOp.details.limitsCreateDetails().dailyOut = preIssuedAmount*3;
                    manageLimitsOp.details.limitsCreateDetails().weeklyOut = preIssuedAmount*6;
                    manageLimitsOp.details.limitsCreateDetails().monthlyOut = preIssuedAmount*12;
                    manageLimitsOp.details.limitsCreateDetails().annualOut = preIssuedAmount*15;
                    manageLimitsTestHelper.applyManageLimitsTx(root, manageLimitsOp);

                    issuanceTasks = 0;
                    auto createIssuanceResult =
                            issuanceRequestHelper.applyCreateIssuanceRequest(issuer, assetToBeIssued, preIssuedAmount*2,
                                                                             issuerBalanceID, reference, &issuanceTasks);
                    REQUIRE(!createIssuanceResult.success().fulfilled);

                    auto requestID = createIssuanceResult.success().requestID;
                    auto request = ReviewableRequestHelper::Instance()->loadRequest(requestID, db);
                    REQUIRE(request->getAllTasks() ==
                             CreateIssuanceRequestOpFrame::INSUFFICIENT_AVAILABLE_FOR_ISSUANCE_AMOUNT);
                }
            }
            SECTION("Issuance request autoapproved")
            {
                issuanceTasks = 0;
                auto createIssuanceResult =
                        issuanceRequestHelper.applyCreateIssuanceRequest(issuer, assetToBeIssued, preIssuedAmount,
                                                                         issuerBalanceID, reference, &issuanceTasks);
            }
            SECTION("Issuance request created. Review issuance request")
            {
                auto assetHelper = ManageAssetTestHelper(testManager);
                assetHelper.updateAsset(issuer, assetToBeIssued, root,
                                        static_cast<uint32_t>(AssetPolicy::ISSUANCE_MANUAL_REVIEW_REQUIRED));
                auto createIssuanceResult =
                        issuanceRequestHelper.applyCreateIssuanceRequest(issuer, assetToBeIssued, preIssuedAmount,
                                                                         issuerBalanceID, reference, &issuanceTasks);
                auto requestID = createIssuanceResult.success().requestID;
                SECTION("Success")
                {
                    auto reviewRequestHelper = ReviewIssuanceRequestHelper(testManager);
                    reviewRequestHelper.applyReviewRequestTx(issuer, requestID, ReviewRequestOpAction::APPROVE, "");
                }
            }
        }
    }

}
