#include <transactions/test/test_helper/SetFeesTestHelper.h>
#include "main/Application.h"
#include "main/Config.h"
#include "util/Timer.h"
#include "overlay/LoopbackPeer.h"
#include "util/make_unique.h"
#include "main/test.h"
#include "TxTests.h"
#include "ledger/BalanceHelper.h"
#include "test/test_marshaler.h"
#include "test_helper/CreateAccountTestHelper.h"
#include "test_helper/IssuanceRequestHelper.h"
#include "test_helper/ManageAssetTestHelper.h"
#include "test_helper/ManageBalanceTestHelper.h"
#include "test_helper/PayoutTestHelper.h"
#include "test_helper/ReviewAssetRequestHelper.h"
#include "test_helper/ReviewIssuanceRequestHelper.h"

using namespace stellar;
using namespace stellar::txtest;

typedef std::unique_ptr<Application> appPtr;

struct HolderAmount{
    Account account;
    uint64_t amount;
};

TEST_CASE("payout", "[tx][payout]") {
    using xdr::operator==;

    Config const &cfg = getTestConfig(0, Config::TESTDB_POSTGRESQL);
    VirtualClock clock;
    Application::pointer appPtr = Application::create(clock, cfg);
    Application &app = *appPtr;
    app.start();
    auto testManager = TestManager::make(app);
    TestManager::upgradeToCurrentLedgerVersion(app);

    // test helpers
    CreateAccountTestHelper createAccountTestHelper(testManager);
    IssuanceRequestHelper issuanceRequestHelper(testManager);
    ManageAssetTestHelper manageAssetTestHelper(testManager);
    ManageBalanceTestHelper manageBalanceTestHelper(testManager);
    PayoutTestHelper payoutTestHelper(testManager);
    ReviewAssetRequestHelper reviewAssetRequestHelper(testManager);
    ReviewIssuanceRequestHelper reviewIssuanceRequestHelper(testManager);
    SetFeesTestHelper setFeesTestHelper(testManager);

    // database
    Database &db = testManager->getDB();
    auto balanceHelper = BalanceHelper::Instance();

    auto root = Account{getRoot(), Salt(0)};

    // create asset owner
    auto owner = Account{SecretKey::random(), Salt(0)};
    auto ownerID = owner.key.getPublicKey();
    createAccountTestHelper.applyCreateAccountTx(root, ownerID, AccountType::SYNDICATE);

    // create asset for payout
    AssetCode assetCode = "EUR";
    uint64_t preIssuedAmount = 1000 * ONE;
    uint64_t maxIssuanceAmount = UINT64_MAX / 2;
    uint64_t assetOwnerAmount = preIssuedAmount / 2;
    SecretKey preIssuedSigner = SecretKey::random();
    auto transferableAssetPolicy = static_cast<uint32>(AssetPolicy::TRANSFERABLE);
    auto assetCreationRequest =
            manageAssetTestHelper.createAssetCreationRequest(assetCode,
                 preIssuedSigner.getPublicKey(), "{}", maxIssuanceAmount,
                 transferableAssetPolicy, preIssuedAmount);
    auto manageAssetResult = manageAssetTestHelper.applyManageAssetTx(owner, 0, assetCreationRequest);
    reviewAssetRequestHelper.applyReviewRequestTx(root, manageAssetResult.success().requestID,
                                                  ReviewRequestOpAction::APPROVE, "");

    auto ownerBalance = balanceHelper->loadBalance(ownerID, assetCode, db);
    REQUIRE(ownerBalance);

    uint32_t issuanceTasks = 0;
    std::string reference = SecretKey::random().getStrKeyPublic();
    issuanceRequestHelper.applyCreateIssuanceRequest(owner, assetCode, assetOwnerAmount,
                                                     ownerBalance->getBalanceID(),
                                                     reference, &issuanceTasks);
    //create fee
    Fee zeroFee;
    zeroFee.fixed = 0;
    zeroFee.percent = 0;

    SECTION("Happy path")
    {
        // create holders and give them some amount of asset
        auto holdersCount = 5;
        HolderAmount holdersAmounts[holdersCount];
        uint64_t assetHoldersAmount = 0;
        for (auto i = 0; i < holdersCount; i++)
        {
            auto newAccount = Account{SecretKey::random(), Salt(0)};
            auto newAccountID = newAccount.key.getPublicKey();
            auto holderAmount = uint64_t(ONE * std::pow(10, i-2));
            createAccountTestHelper.applyCreateAccountTx(root, newAccountID,
                                                         AccountType::GENERAL);
            manageBalanceTestHelper.createBalance(newAccount,
                                                  newAccountID, assetCode);
            auto newBalance = balanceHelper->loadBalance(newAccountID,
                    assetCode, db);
            REQUIRE(newBalance != nullptr);
            reference = SecretKey::random().getStrKeyPublic();
            issuanceRequestHelper.applyCreateIssuanceRequest(owner, assetCode,
                                       holderAmount, newBalance->getBalanceID(),
                                       reference, &issuanceTasks);
            holdersAmounts[i] = {newAccount, holderAmount};
            assetHoldersAmount += holderAmount;
        }

        SECTION("Pay with own asset")
        {
            uint64_t maxPayoutAmount = assetOwnerAmount / 10;

            BalanceFrame::pointer holdersBalancesBefore[holdersCount];
            for (auto i = 0; i < holdersCount; i++)
            {
                holdersBalancesBefore[i] = balanceHelper->loadBalance(
                        holdersAmounts[i].account.key.getPublicKey(),
                        assetCode, db);
            }

            auto receiversCount = 0;

            SECTION("Zero fee")
            {
                SECTION("Payout with zero min payout amount and "
                        "zero min asset holder amount")
                {
                    auto result = payoutTestHelper.applyPayoutTx(owner,
                            assetCode, ownerBalance->getBalanceID(),
                            maxPayoutAmount, 0, 0, zeroFee);

                    for (auto i = 0; i < holdersCount; i++)
                    {
                        auto holderBalanceAfter = balanceHelper->loadBalance(
                                holdersAmounts[i].account.key.getPublicKey(),
                                assetCode, db);
                        uint64_t receivedAmount = 0;
                        REQUIRE(bigDivide(receivedAmount, maxPayoutAmount,
                                          holdersAmounts[i].amount,
                                          assetHoldersAmount, ROUND_DOWN));
                        REQUIRE(holderBalanceAfter->getAmount() ==
                                holdersBalancesBefore[i]->getAmount() +
                                receivedAmount);
                        receiversCount++;
                    }

                    REQUIRE(receiversCount == 5);
                }

                SECTION("Payout with non-zero min payout amount and "
                        "zero min asset holder amount")
                {
                    uint64_t minPayoutAmount = ONE;
                    auto result = payoutTestHelper.applyPayoutTx(owner,
                            assetCode, ownerBalance->getBalanceID(),
                            maxPayoutAmount, minPayoutAmount, 0, zeroFee);

                    for (auto i = 0; i < holdersCount; i++)
                    {
                        auto holderBalanceAfter = balanceHelper->loadBalance(
                                holdersAmounts[i].account.key.getPublicKey(),
                                assetCode, db);
                        uint64_t receivedAmount = 0;
                        REQUIRE(bigDivide(receivedAmount, maxPayoutAmount,
                                          holdersAmounts[i].amount,
                                          assetHoldersAmount, ROUND_DOWN));

                        if (receivedAmount < minPayoutAmount)
                            continue;

                        REQUIRE(holderBalanceAfter->getAmount() ==
                                holdersBalancesBefore[i]->getAmount() +
                                receivedAmount);
                        receiversCount++;
                    }

                    REQUIRE(receiversCount == 2);
                }

                SECTION("Payout with zero min payout amount and "
                        "non-zero min asset holder amount")
                {
                    uint64_t minAssetHolderAmount = ONE;
                    auto result = payoutTestHelper.applyPayoutTx(owner,
                            assetCode, ownerBalance->getBalanceID(),
                            maxPayoutAmount, 0, minAssetHolderAmount, zeroFee);

                    for (auto i = 0; i < holdersCount; i++)
                    {
                        if (holdersBalancesBefore[i]->getTotal() <
                            minAssetHolderAmount)
                            continue;

                        auto holderBalanceAfter = balanceHelper->loadBalance(
                                holdersAmounts[i].account.key.getPublicKey(),
                                assetCode, db);
                        uint64_t receivedAmount = 0;
                        REQUIRE(bigDivide(receivedAmount, maxPayoutAmount,
                                          holdersAmounts[i].amount,
                                          assetHoldersAmount, ROUND_DOWN));
                        REQUIRE(holderBalanceAfter->getAmount() ==
                                holdersBalancesBefore[i]->getAmount() +
                                receivedAmount);
                        receiversCount++;
                    }

                    REQUIRE(receiversCount == 3);
                }

                SECTION("Payout with non-zero min payout amount and "
                        "non-zero min asset holder amount")
                {
                    uint64_t minPayoutAmount = ONE;
                    uint64_t minAssetHolderAmount = 20 * ONE;
                    payoutTestHelper.applyPayoutTx(owner, assetCode,
                            ownerBalance->getBalanceID(), maxPayoutAmount,
                            minPayoutAmount, minAssetHolderAmount, zeroFee);

                    for (auto i = 0; i < holdersCount; i++)
                    {
                        if (holdersBalancesBefore[i]->getTotal() <
                            minAssetHolderAmount)
                            continue;

                        auto holderBalanceAfter = balanceHelper->loadBalance(
                                holdersAmounts[i].account.key.getPublicKey(),
                                assetCode, db);
                        uint64_t receivedAmount = 0;
                        REQUIRE(bigDivide(receivedAmount, maxPayoutAmount,
                                          holdersAmounts[i].amount,
                                          assetHoldersAmount, ROUND_DOWN));

                        if (receivedAmount < minPayoutAmount)
                            continue;

                        REQUIRE(holderBalanceAfter->getAmount() ==
                                holdersBalancesBefore[i]->getAmount() +
                                receivedAmount);
                        receiversCount++;
                    }

                    REQUIRE(receiversCount == 1);
                }
            }

            SECTION("Non-zero fee")
            {
                auto feeEntry = setFeesTestHelper.createFeeEntry(
                        FeeType::PAYOUT_FEE, assetCode, 20 * ONE, 1 * ONE);
                setFeesTestHelper.applySetFeesTx(root, &feeEntry, false);

                Fee fee;
                fee.fixed = feeEntry.fixedFee;
                REQUIRE(bigDivide(fee.percent, feeEntry.percentFee,
                                  maxPayoutAmount, 100 * ONE, ROUND_UP));

                payoutTestHelper.applyPayoutTx(owner, assetCode,
                      ownerBalance->getBalanceID(), maxPayoutAmount, 0, 0, fee);

                for (auto i = 0; i < holdersCount; i++)
                {
                    auto holderBalanceAfter = balanceHelper->loadBalance(
                            holdersAmounts[i].account.key.getPublicKey(),
                            assetCode, db);
                    uint64_t receivedAmount = 0;
                    REQUIRE(bigDivide(receivedAmount, maxPayoutAmount,
                                      holdersAmounts[i].amount,
                                      assetHoldersAmount, ROUND_DOWN));

                    REQUIRE(holderBalanceAfter->getAmount() ==
                            holdersBalancesBefore[i]->getAmount() +
                            receivedAmount);
                    receiversCount++;
                }

                REQUIRE(receiversCount == 5);
            }
        }

        SECTION("Pay with any third-party asset")
        {
            // create third-party owner and his asset
            auto thirdPartyIssuer = Account {SecretKey::random(), Salt(0)};
            auto thirdPartyIssuerID = thirdPartyIssuer.key.getPublicKey();
            createAccountTestHelper.applyCreateAccountTx(root,
                                 thirdPartyIssuerID, AccountType::SYNDICATE);
            AssetCode thirdPartyAssetCode = "USD";
            preIssuedAmount = 1000 * ONE;
            issuanceRequestHelper.createAssetWithPreIssuedAmount(root,
                                 thirdPartyAssetCode, preIssuedAmount, root);
            manageAssetTestHelper.updateAsset(root, thirdPartyAssetCode, root,
                              static_cast<uint32_t>(AssetPolicy::TRANSFERABLE));
            auto thirdPartyIssuerBalance = balanceHelper->
                    loadBalance(thirdPartyIssuerID, thirdPartyAssetCode, db);
            reference = SecretKey::random().getStrKeyPublic();
            issuanceRequestHelper.applyCreateIssuanceRequest(thirdPartyIssuer, thirdPartyAssetCode, 300 * ONE,
                                                             thirdPartyIssuerBalance->getBalanceID(), reference, &issuanceTasks);

            // create balance of third-party asset for owner
            manageBalanceTestHelper.createBalance(owner, ownerID, thirdPartyAssetCode);
            auto issuerThirdPartyBalance = BalanceHelper::Instance()->loadBalance(ownerID, thirdPartyAssetCode,
                                                                                  db, nullptr);
            reference = SecretKey::random().getStrKeyPublic();
            issuanceRequestHelper.applyCreateIssuanceRequest(thirdPartyIssuer, thirdPartyAssetCode, 200 * ONE,
                                                             issuerThirdPartyBalance->getBalanceID(), reference, &issuanceTasks);
            SECTION("Zero fee") {
                payoutTestHelper.applyPayoutTx(owner, assetCode, issuerThirdPartyBalance->getBalanceID(), 100 * ONE,
                                               0, 0, zeroFee);

                issuerThirdPartyBalance = BalanceHelper::Instance()->loadBalance(ownerID, thirdPartyAssetCode, db,
                                                                                 nullptr);
                auto holder1ThirdPartyBalance = BalanceHelper::Instance()->loadBalance(holdersAmounts[3].account.key.getPublicKey(), thirdPartyAssetCode, db,
                                                                                       nullptr);
                auto holder2ThirdPartyBalance = BalanceHelper::Instance()->loadBalance(holdersAmounts[4].account.key.getPublicKey(), thirdPartyAssetCode, db,
                                                                                       nullptr);

                REQUIRE(issuerThirdPartyBalance->getAmount() == 170 * ONE);
                REQUIRE(holder1ThirdPartyBalance->getAmount() == 20 * ONE);
                REQUIRE(holder2ThirdPartyBalance->getAmount() == 10 * ONE);
            }

            SECTION("Non-zero fee") {
                auto payoutFeeFrame = FeeFrame::create(FeeType::PAYOUT_FEE, 10 * ONE,
                                                       int64_t(5 * ONE), thirdPartyAssetCode,
                                                       &ownerID);
                auto payoutFee = payoutFeeFrame->getFee();
                applySetFees(app, root.key, root.salt, &payoutFee, false, nullptr);

                Fee fee;
                fee.fixed = 10 * ONE;
                fee.percent = 5 * ONE;

                payoutTestHelper.applyPayoutTx(owner, assetCode, issuerThirdPartyBalance->getBalanceID(), 100 * ONE,
                                               0, 0, fee);

                issuerThirdPartyBalance = BalanceHelper::Instance()->loadBalance(ownerID, thirdPartyAssetCode, db,
                                                                                 nullptr);
                auto commissionBalance = BalanceHelper::Instance()->loadBalance(getCommissionKP().getPublicKey(),
                                                                                thirdPartyAssetCode, db, nullptr);
                REQUIRE(issuerThirdPartyBalance->getAmount() == 155 * ONE);
                REQUIRE(commissionBalance->getAmount() == 15 * ONE);
            }
        }
    }

    SECTION("Invalid amount") {
        payoutTestHelper.applyPayoutTx(owner, assetCode, ownerBalance->getBalanceID(), 0, 0, 0, zeroFee,
                                       PayoutResultCode::INVALID_AMOUNT);
    }

    SECTION("Invalid asset code") {
        payoutTestHelper.applyPayoutTx(owner, "", ownerBalance->getBalanceID(), 100 * ONE, 0, 0, zeroFee,
                                       PayoutResultCode::INVALID_ASSET);
    }

    SECTION("Asset not found") {
        payoutTestHelper.applyPayoutTx(owner, "USD", ownerBalance->getBalanceID(), 100 * ONE, 0, 0, zeroFee,
                                       PayoutResultCode::ASSET_NOT_FOUND);
    }

    SECTION("Balance not found")
    {
        auto account = SecretKey::random();
        payoutTestHelper.applyPayoutTx(owner, assetCode, account.getPublicKey(),
                                       100 * ONE, 0, 0, zeroFee,
                                       PayoutResultCode::BALANCE_NOT_FOUND);
    }

    SECTION("Balance account mismatched") {
        Account account = {SecretKey::random(), Salt(0)};
        auto accountID = account.key.getPublicKey();
        createAccountTestHelper.applyCreateAccountTx(root, accountID, AccountType::SYNDICATE);
        manageBalanceTestHelper.createBalance(account, accountID, assetCode);
        auto accountBalance = BalanceHelper::Instance()->loadBalance(accountID, assetCode, db, nullptr);
        payoutTestHelper.applyPayoutTx(owner, assetCode, accountBalance->getBalanceID(), 100 * ONE, 0, 0, zeroFee,
                                       PayoutResultCode::BALANCE_NOT_FOUND);
    }

    /*SECTION("Fee mismatched") {
        Fee giantFee;
        giantFee.fixed = preIssuedAmount;
        giantFee.percent = 0;
        payoutTestHelper.applyPayoutTx(owner, assetCode, ownerBalance->getBalanceID(), 100 * ONE, 0,
                                       giantFee, PayoutResultCode::FEE_MISMATCHED);
    }*/

    SECTION("Holders not found") {
        payoutTestHelper.applyPayoutTx(owner, assetCode, ownerBalance->getBalanceID(), 100 * ONE, 0, 0, zeroFee,
                                       PayoutResultCode::HOLDERS_NOT_FOUND);
    }


}