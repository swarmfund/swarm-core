// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0
#include <transactions/test/test_helper/CreateAccountTestHelper.h>
#include <transactions/test/test_helper/ManageAssetPairTestHelper.h>
#include <ledger/AccountHelper.h>
#include <ledger/FeeHelper.h>
#include <ledger/ReviewableRequestHelper.h>
#include "main/test.h"
#include "TxTests.h"
#include "crypto/SHA.h"
#include "test_helper/ManageAssetTestHelper.h"
#include "test_helper/IssuanceRequestHelper.h"
#include "ledger/BalanceHelper.h"
#include "test_helper/WithdrawRequestHelper.h"
#include "test_helper/ReviewWithdrawalRequestHelper.h"
#include "test/test_marshaler.h"

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
    const uint64_t statsPricePerUnit = 25;
    const uint64_t statsPrice = 25 * ONE;
    assetPairHelper.createAssetPair(root, asset, statsAsset, statsPrice);

    // create account which will withdraw
    auto withdrawerKP = SecretKey::random();
    createAccountTestHelper.applyCreateAccountTx(root, withdrawerKP.getPublicKey(), AccountType::GENERAL);
    auto withdrawer = Account{ withdrawerKP, Salt(0) };
    auto withdrawerBalance = BalanceHelper::Instance()->loadBalance(withdrawerKP.getPublicKey(), asset, testManager->getDB(), nullptr);
    REQUIRE(!!withdrawerBalance);
    issuanceHelper.applyCreateIssuanceRequest(root, asset, preIssuedAmount, withdrawerBalance->getBalanceID(), "RANDOM ISSUANCE REFERENCE");

    SECTION("Happy path")
    {
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

        Fee zeroFee;
        zeroFee.fixed = 0;
        zeroFee.percent = 0;
        auto withdrawRequest = withdrawRequestHelper.createWithdrawRequest(withdrawerBalance->getBalanceID(), amountToWithdraw,
                                                                           zeroFee, "{}", withdrawDestAsset,
                                                                           expectedAmountInDestAsset);

        auto withdrawResult = withdrawRequestHelper.applyCreateWithdrawRequest(withdrawer, withdrawRequest);
        SECTION("Approve")
        {
            reviewWithdrawHelper.applyReviewRequestTx(root, withdrawResult.success().requestID, ReviewRequestOpAction::APPROVE, "");
        }
        SECTION("Reject")
        {
            reviewWithdrawHelper.applyReviewRequestTx(root, withdrawResult.success().requestID, ReviewRequestOpAction::PERMANENT_REJECT,
                "Invalid external details");
        }
        SECTION("Set withdrawal fee and approve")
        {
            uint64_t percentFee = 1 * ONE;
            uint64_t fixedFee = amountToWithdraw/2;
            AccountID account = withdrawerBalance->getAccountID();
            auto feeEntry = createFeeEntry(FeeType::WITHDRAWAL_FEE, fixedFee, percentFee, asset, &account, nullptr);
            applySetFees(testManager->getApp(), root.key, root.getNextSalt(), &feeEntry, false);

            auto accountFrame = AccountHelper::Instance()->loadAccount(account, testManager->getDB());
            auto feeFrame = FeeHelper::Instance()->loadForAccount(FeeType::WITHDRAWAL_FEE, asset, FeeFrame::SUBTYPE_ANY,
                                                                  accountFrame, preIssuedAmount, testManager->getDB());
            REQUIRE(feeFrame);

            Fee fee;
            fee.fixed = fixedFee;
            REQUIRE(feeFrame->calculatePercentFee(amountToWithdraw, fee.percent, ROUND_UP));
            auto withdrawWithFeeRequest = withdrawRequestHelper.createWithdrawRequest(withdrawerBalance->getBalanceID(),
                                                                                      amountToWithdraw, fee, "{}",
                                                                                      withdrawDestAsset, expectedAmountInDestAsset);
            auto result = withdrawRequestHelper.applyCreateWithdrawRequest(withdrawer, withdrawWithFeeRequest);

            //approve request
            reviewWithdrawHelper.applyReviewRequestTx(root, result.success().requestID, ReviewRequestOpAction::APPROVE, "");
        }
    }

    SECTION("create withdraw request hard path")
    {
        // create asset to withdraw to
        const AssetCode withdrawDestAsset = "CZK";
        assetHelper.createAsset(root, root.key, withdrawDestAsset, root, 0);
        const uint64_t pricePerUnit = 20;
        const uint64_t price = pricePerUnit * ONE;
        assetPairHelper.createAssetPair(root, asset, withdrawDestAsset, price);

        //create withdraw request
        uint64_t amountToWithdraw = 1000 * ONE;
        withdrawerBalance = BalanceHelper::Instance()->loadBalance(withdrawerKP.getPublicKey(), asset, testManager->getDB(), nullptr);
        REQUIRE(withdrawerBalance->getAmount() >= amountToWithdraw);
        const uint64_t expectedAmountInDestAsset = pricePerUnit * amountToWithdraw;

        Fee zeroFee;
        zeroFee.fixed = 0;
        zeroFee.percent = 0;
        auto withdrawRequest = withdrawRequestHelper.createWithdrawRequest(withdrawerBalance->getBalanceID(), amountToWithdraw,
                                                                           zeroFee, "{}", withdrawDestAsset,
                                                                           expectedAmountInDestAsset);

        SECTION("successful application")
        {
            withdrawRequestHelper.applyCreateWithdrawRequest(withdrawer, withdrawRequest);
        }

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

        SECTION("invalid external details json")
        {
            //missed colon
            std::string invalidExternalDetails = "{ \"key\" \"value\" }";
            withdrawRequest.externalDetails = invalidExternalDetails;
            withdrawRequestHelper.applyCreateWithdrawRequest(withdrawer, withdrawRequest,
                                                             CreateWithdrawalRequestResultCode::INVALID_EXTERNAL_DETAILS);
        }

        SECTION("try to review with invalid external details")
        {
            auto opRes = withdrawRequestHelper.applyCreateWithdrawRequest(withdrawer, withdrawRequest);
            uint64_t requestID = opRes.success().requestID;

            auto requestFrame = ReviewableRequestHelper::Instance()->loadRequest(requestID, testManager->getDB());
            REQUIRE(!!requestFrame);

            Operation op;
            op.body.type(OperationType::REVIEW_REQUEST);
            auto& reviewWithdraw = op.body.reviewRequestOp();
            reviewWithdraw.requestID = requestID;
            reviewWithdraw.action = ReviewRequestOpAction::APPROVE;
            reviewWithdraw.reason = "";
            reviewWithdraw.requestHash = requestFrame->getHash();
            reviewWithdraw.requestDetails.requestType(ReviewableRequestType::WITHDRAW);
            reviewWithdraw.requestDetails.withdrawal().externalDetails = "{\"key\"}";

            TxHelper txHelper(testManager);
            auto txFrame = txHelper.txFromOperation(root, op);
            testManager->applyCheck(txFrame);

            auto opResCode = txFrame->getResult().result.results()[0].tr().reviewRequestResult().code();
            REQUIRE(opResCode == ReviewRequestResultCode::INVALID_EXTERNAL_DETAILS);
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
            Fee newFee;
            newFee.fixed = ONE;
            newFee.percent = ONE;
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

        SECTION("overflow converted amount")
        {
            withdrawRequest.amount = UINT64_MAX/pricePerUnit + 1;
            withdrawRequestHelper.applyCreateWithdrawRequest(withdrawer, withdrawRequest,
                                                             CreateWithdrawalRequestResultCode::CONVERSION_OVERFLOW);
        }

        SECTION("mismatch conversion amount")
        {
            withdrawRequest.details.autoConversion().expectedAmount = expectedAmountInDestAsset + 1;
            withdrawRequestHelper.applyCreateWithdrawRequest(withdrawer, withdrawRequest,
                                                             CreateWithdrawalRequestResultCode::CONVERTED_AMOUNT_MISMATCHED);
        }

        SECTION("underfunded")
        {
            withdrawRequest.amount = preIssuedAmount + 1;
            uint64_t convertedAmount = pricePerUnit * withdrawRequest.amount;
            withdrawRequest.details.autoConversion().expectedAmount = convertedAmount;

            withdrawRequestHelper.applyCreateWithdrawRequest(withdrawer, withdrawRequest,
                                                             CreateWithdrawalRequestResultCode::UNDERFUNDED);
        }

        SECTION("overflow statistics")
        {
            //issue some amount to withdrawer
            uint64_t enoughToOverflow = UINT64_MAX/pricePerUnit - 1;
            REQUIRE(statsPricePerUnit > pricePerUnit);
            issuanceHelper.authorizePreIssuedAmount(root, root.key, asset, enoughToOverflow, root);
            issuanceHelper.applyCreateIssuanceRequest(root, asset, enoughToOverflow, withdrawerBalance->getBalanceID(),
                                                      SecretKey::random().getStrKeyPublic());
            withdrawRequest.amount = enoughToOverflow;
            withdrawRequest.details.autoConversion().expectedAmount = enoughToOverflow * pricePerUnit;
            withdrawRequestHelper.applyCreateWithdrawRequest(withdrawer, withdrawRequest,
                                                             CreateWithdrawalRequestResultCode::STATS_OVERFLOW);
        }

        SECTION("exceed limits")
        {
            AccountManager accountManager(app, testManager->getDB(), testManager->getLedgerDelta(),
                                          testManager->getLedgerManager());
            Limits limits = accountManager.getDefaultLimits(AccountType::GENERAL);
            limits.dailyOut = amountToWithdraw - 1;
            AccountID withdrawerID = withdrawer.key.getPublicKey();

            //set limits for withdrawer
            applySetLimits(testManager->getApp(), root.key, root.getNextSalt(), &withdrawerID, nullptr, limits);

            withdrawRequestHelper.applyCreateWithdrawRequest(withdrawer, withdrawRequest,
                                                             CreateWithdrawalRequestResultCode::LIMITS_EXCEEDED);
        }

    }

}
