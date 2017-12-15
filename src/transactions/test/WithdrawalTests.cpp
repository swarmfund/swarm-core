// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0
#include <transactions/test/test_helper/CreateAccountTestHelper.h>
#include <transactions/test/test_helper/ManageAssetPairTestHelper.h>
#include "main/test.h"
#include "TxTests.h"
#include "crypto/SHA.h"
#include "test_helper/ManageAssetTestHelper.h"
#include "test_helper/IssuanceRequestHelper.h"
#include "ledger/BalanceHelper.h"
#include "test_helper/WithdrawRequestHelper.h"
#include "test_helper/ReviewWithdrawalRequestHelper.h"

using namespace stellar;
using namespace stellar::txtest;

typedef std::unique_ptr<Application> appPtr;

TEST_CASE("Manage forfeit request", "[tx][withdraw]")
{
    Config const& cfg = getTestConfig(0, Config::TESTDB_POSTGRESQL);
    VirtualClock clock;
    Application::pointer appPtr = Application::create(clock, cfg);
    Application& app = *appPtr;
    app.start();
    auto testManager = TestManager::make(app);

    auto root = Account{ getRoot(), Salt(0) };
    auto issuanceHelper = IssuanceRequestHelper(testManager);
    auto assetHelper = ManageAssetTestHelper(testManager);
    auto assetPairHelper = ManageAssetPairTestHelper(testManager);
    auto reviewWithdrawHelper = ReviewWithdrawRequestHelper(testManager);
    auto withdrawRequestHelper = WithdrawRequestHelper(testManager);
    auto createAccountTestHelper = CreateAccountTestHelper(testManager);

    // create asset and make it withdrawable
    const AssetCode asset = "USD";
    const uint64_t preIssuedAmount = 10000 * ONE;
    issuanceHelper.createAssetWithPreIssuedAmount(root, asset, preIssuedAmount, root);
    assetHelper.updateAsset(root, asset, root, static_cast<uint32_t>(AssetPolicy::BASE_ASSET) | static_cast<uint32_t>(AssetPolicy::WITHDRAWABLE));

    //create stats asset and stats asset pair
    const AssetCode statsAsset = "UAH";
    assetHelper.createAsset(root, root.key, statsAsset, root, static_cast<uint32_t>(AssetPolicy::STATS_QUOTE_ASSET));
    const uint64_t statsPrice = 25 * ONE;
    assetPairHelper.createAssetPair(root, asset, statsAsset, statsPrice);

    // create account which will withdraw
    auto withdrawerKP = SecretKey::random();
    createAccountTestHelper.applyCreateAccountTx(root, withdrawerKP.getPublicKey(), AccountType::GENERAL);
    auto withdrawer = Account{ withdrawerKP, Salt(0) };
    auto withdrawerBalance = BalanceHelper::Instance()->loadBalance(withdrawerKP.getPublicKey(), asset, testManager->getDB(), nullptr);
    REQUIRE(!!withdrawerBalance);
    issuanceHelper.applyCreateIssuanceRequest(root, asset, preIssuedAmount, withdrawerBalance->getBalanceID(), "RANDOM ISSUANCE REFERENCE");

    // create asset to withdraw to
    const AssetCode withdrawDestAsset = "BTC";
    assetHelper.createAsset(root, root.key, withdrawDestAsset, root, 0);
    const uint64_t price = 2000 * ONE;
    assetPairHelper.createAssetPair(root, withdrawDestAsset, asset, price);

    //create withdraw request
    uint64_t amountToWithdraw = 1000 * ONE;
    withdrawerBalance = BalanceHelper::Instance()->loadBalance(withdrawerKP.getPublicKey(), asset, testManager->getDB(), nullptr);
    REQUIRE(withdrawerBalance->getAmount() >= amountToWithdraw);
    const uint64_t expectedAmountInDestAsset = 0.5 * ONE;

    auto withdrawRequest = withdrawRequestHelper.createWithdrawRequest(withdrawerBalance->getBalanceID(), amountToWithdraw,
                                                                             Fee{ 0, 0 }, "{}", withdrawDestAsset,
                                                                             expectedAmountInDestAsset);

    SECTION("Happy path")
    {
        auto withdrawResult = WithdrawRequestHelper(testManager).applyCreateWithdrawRequest(withdrawer, withdrawRequest);
        SECTION("Approve")
        {
            reviewWithdrawHelper.applyReviewRequestTx(root, withdrawResult.success().requestID, ReviewRequestOpAction::APPROVE, "");
        }
        SECTION("Reject")
        {
            reviewWithdrawHelper.applyReviewRequestTx(root, withdrawResult.success().requestID, ReviewRequestOpAction::PERMANENT_REJECT,
                "Invalid external details");
        }
    }

    SECTION("create withdraw request hard path")
    {
        SECTION("Try to withdraw zero amount")
        {
            withdrawRequest.amount = 0;
            withdrawRequestHelper.applyCreateWithdrawRequest(withdrawer, withdrawRequest, CreateWithdrawalRequestResultCode::INVALID_AMOUNT);
        }

        SECTION("too long external details")
        {
            uint64 maxLength = testManager->getApp().getWithdrawalDetailsMaxLength();
            std::string longExternalDetails(maxLength + 1, 'x');
            withdrawRequest.externalDetails = longExternalDetails;
            withdrawRequestHelper.applyCreateWithdrawRequest(withdrawer, withdrawRequest,
                                                             CreateWithdrawalRequestResultCode::INVALID_EXTERNAL_DETAILS);
        }

        SECTION("non-zero universal amount")
        {
            withdrawRequest.universalAmount = ONE;
            withdrawRequestHelper.applyCreateWithdrawRequest(withdrawer, withdrawRequest,
                                                             CreateWithdrawalRequestResultCode::INVALID_UNIVERSAL_AMOUNT);
        }

        SECTION("try to withdraw from non-existing balance")
        {
            BalanceID nonExistingBalance = SecretKey::random().getPublicKey();
            withdrawRequest.balance = nonExistingBalance;
            withdrawRequestHelper.applyCreateWithdrawRequest(withdrawer, withdrawRequest,
                                                             CreateWithdrawalRequestResultCode::BALANCE_NOT_FOUND);
        }

        SECTION("try to withdraw from not my balance")
        {
            //create another account
            auto newAccountKP = SecretKey::random();
            createAccountTestHelper.applyCreateAccountTx(root, newAccountKP.getPublicKey(), AccountType::GENERAL);
            Account newAccount = Account{newAccountKP, Salt(0)};

            withdrawRequestHelper.applyCreateWithdrawRequest(newAccount, withdrawRequest,
                                                             CreateWithdrawalRequestResultCode::BALANCE_NOT_FOUND);
        }

        SECTION("try to withdraw non-withdrawable asset")
        {
            // make asset non-withdrawable by updating policies
            assetHelper.updateAsset(root, asset, root, static_cast<uint32_t>(AssetPolicy::BASE_ASSET));
            withdrawRequestHelper.applyCreateWithdrawRequest(withdrawer, withdrawRequest,
                                                             CreateWithdrawalRequestResultCode::ASSET_IS_NOT_WITHDRAWABLE);
        }

        SECTION("fee doesn't match")
        {
            Fee newFee{ONE, ONE};
            withdrawRequest.fee = newFee;
            withdrawRequestHelper.applyCreateWithdrawRequest(withdrawer, withdrawRequest,
                                                             CreateWithdrawalRequestResultCode::FEE_MISMATCHED);
        }

        SECTION("try to withdraw in non-existing asset")
        {
            AssetCode nonExistingAsset = "BYN";
            withdrawRequest.details.autoConversion().destAsset = nonExistingAsset;
            withdrawRequestHelper.applyCreateWithdrawRequest(withdrawer, withdrawRequest,
                                                             CreateWithdrawalRequestResultCode::CONVERSION_PRICE_IS_NOT_AVAILABLE);
        }

    }

}
