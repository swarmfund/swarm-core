#include <transactions/test/test_helper/SetFeesTestHelper.h>
#include <transactions/test/test_helper/ManageLimitsTestHelper.h>
#include <ledger/BalanceHelperLegacy.h>
#include <ledger/LedgerDeltaImpl.h>
#include <ledger/StorageHelperImpl.h>
#include "main/Application.h"
#include "main/Config.h"
#include "util/Timer.h"
#include "overlay/LoopbackPeer.h"
#include "util/make_unique.h"
#include "main/test.h"
#include "TxTests.h"
#include "ledger/BalanceHelper.h"
#include "ledger/AssetHelper.h"
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
    ManageLimitsTestHelper manageLimitsTestHelper(testManager);

    // storage
    Database& db = testManager->getDB();
    LedgerDeltaImpl deltaImpl(testManager->getLedgerManager().getCurrentLedgerHeader(), db);
    LedgerDelta& delta = deltaImpl;
    StorageHelperImpl storageHelperImpl(db, &delta);
    StorageHelper& storageHelper = storageHelperImpl;
    auto& balanceHelper = storageHelper.getBalanceHelper();
    auto& assetHelper = storageHelper.getAssetHelper();

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

    auto ownerBalance = balanceHelper.loadBalance(ownerID, assetCode);
    REQUIRE(ownerBalance);
    auto ownerBalanceID = ownerBalance->getBalanceID();

    uint32_t issuanceTasks = 0;
    std::string reference = SecretKey::random().getStrKeyPublic();
    issuanceRequestHelper.applyCreateIssuanceRequest(owner, assetCode,
             assetOwnerAmount, ownerBalanceID, reference, &issuanceTasks);
    //create fee
    Fee zeroFee;
    zeroFee.fixed = 0;
    zeroFee.percent = 0;

    SECTION("Happy path")
    {
        // create holders and give them some amount of asset
        auto holdersCount = 5;
        HolderAmount holdersAmounts[holdersCount];
        for (auto i = 0; i < holdersCount; i++)
        {
            auto newAccount = Account{SecretKey::random(), Salt(0)};
            auto newAccountID = newAccount.key.getPublicKey();
            auto holderAmount = uint64_t(ONE * std::pow(10, i-2));
            createAccountTestHelper.applyCreateAccountTx(root, newAccountID,
                                                         AccountType::GENERAL);
            manageBalanceTestHelper.createBalance(newAccount,
                                                  newAccountID, assetCode);
            auto newBalance = balanceHelper.loadBalance(newAccountID,
                    assetCode);
            REQUIRE(newBalance != nullptr);
            reference = SecretKey::random().getStrKeyPublic();
            issuanceRequestHelper.applyCreateIssuanceRequest(owner, assetCode,
                                       holderAmount, newBalance->getBalanceID(),
                                       reference, &issuanceTasks);
            holdersAmounts[i] = {newAccount, holderAmount};
        }

        auto assetFrame = assetHelper.loadAsset(assetCode);
        REQUIRE(assetFrame != nullptr);
        REQUIRE(assetFrame->getIssued() != 0);

        SECTION("Pay with own asset")
        {
            uint64_t maxPayoutAmount = assetOwnerAmount / 10;

            BalanceFrame::pointer holdersBalancesBefore[holdersCount];
            for (auto i = 0; i < holdersCount; i++)
            {
                holdersBalancesBefore[i] = balanceHelper.loadBalance(
                        holdersAmounts[i].account.key.getPublicKey(),
                        assetCode);
            }

            auto receiversCount = 0;

            SECTION("Zero fee")
            {
                SECTION("Payout with zero min payout amount and "
                        "zero min asset holder amount")
                {
                    auto result = payoutTestHelper.applyPayoutTx(owner,
                            assetCode, ownerBalanceID,
                            maxPayoutAmount, 0, 0, zeroFee);

                    for (auto i = 0; i < holdersCount; i++)
                    {
                        auto holderBalanceAfter = balanceHelper.loadBalance(
                                holdersAmounts[i].account.key.getPublicKey(),
                                assetCode);
                        uint64_t receivedAmount = 0;
                        REQUIRE(bigDivide(receivedAmount, maxPayoutAmount,
                                          holdersAmounts[i].amount,
                                          assetFrame->getIssued(), ROUND_DOWN));
                        REQUIRE(holderBalanceAfter->getAmount() ==
                                holdersBalancesBefore[i]->getAmount() +
                                receivedAmount);
                        receiversCount++;
                    }

                    REQUIRE(receiversCount ==
                            result.success().payoutResponses.size());
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
                        auto holderBalanceAfter = balanceHelper.loadBalance(
                                holdersAmounts[i].account.key.getPublicKey(),
                                assetCode);
                        uint64_t receivedAmount = 0;
                        REQUIRE(bigDivide(receivedAmount, maxPayoutAmount,
                                          holdersAmounts[i].amount,
                                          assetFrame->getIssued(), ROUND_DOWN));

                        if (receivedAmount < minPayoutAmount)
                            continue;

                        REQUIRE(holderBalanceAfter->getAmount() ==
                                holdersBalancesBefore[i]->getAmount() +
                                receivedAmount);
                        receiversCount++;
                    }

                    REQUIRE(receiversCount ==
                            result.success().payoutResponses.size());
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

                        auto holderBalanceAfter = balanceHelper.loadBalance(
                                holdersAmounts[i].account.key.getPublicKey(),
                                assetCode);
                        uint64_t receivedAmount = 0;
                        REQUIRE(bigDivide(receivedAmount, maxPayoutAmount,
                                          holdersAmounts[i].amount,
                                          assetFrame->getIssued(), ROUND_DOWN));
                        REQUIRE(holderBalanceAfter->getAmount() ==
                                holdersBalancesBefore[i]->getAmount() +
                                receivedAmount);
                        receiversCount++;
                    }

                    REQUIRE(receiversCount ==
                            result.success().payoutResponses.size());
                }

                SECTION("Payout with non-zero min payout amount and "
                        "non-zero min asset holder amount")
                {
                    uint64_t minPayoutAmount = ONE;
                    uint64_t minAssetHolderAmount = 20 * ONE;
                    auto result = payoutTestHelper.applyPayoutTx(owner,
                            assetCode, ownerBalance->getBalanceID(),
                            maxPayoutAmount, minPayoutAmount,
                            minAssetHolderAmount, zeroFee);

                    for (auto i = 0; i < holdersCount; i++)
                    {
                        if (holdersBalancesBefore[i]->getTotal() <
                            minAssetHolderAmount)
                            continue;

                        auto holderBalanceAfter = balanceHelper.loadBalance(
                                holdersAmounts[i].account.key.getPublicKey(),
                                assetCode);
                        uint64_t receivedAmount = 0;
                        REQUIRE(bigDivide(receivedAmount, maxPayoutAmount,
                                          holdersAmounts[i].amount,
                                          assetFrame->getIssued(), ROUND_DOWN));

                        if (receivedAmount < minPayoutAmount)
                            continue;

                        REQUIRE(holderBalanceAfter->getAmount() ==
                                holdersBalancesBefore[i]->getAmount() +
                                receivedAmount);
                        receiversCount++;
                    }

                    REQUIRE(receiversCount ==
                            result.success().payoutResponses.size());
                }
            }

            SECTION("Non-zero fee")
            {
                auto feeEntry = setFeesTestHelper.createFeeEntry(
                        FeeType::PAYOUT_FEE, assetCode, ONE, ONE);
                setFeesTestHelper.applySetFeesTx(root, &feeEntry, false);

                Fee fee;
                fee.fixed = feeEntry.fixedFee;
                REQUIRE(bigDivide(fee.percent, feeEntry.percentFee,
                                  maxPayoutAmount, 100 * ONE, ROUND_UP));

                auto result = payoutTestHelper.applyPayoutTx(owner, assetCode,
                      ownerBalance->getBalanceID(), maxPayoutAmount, 0, 0, fee);

                for (auto i = 0; i < holdersCount; i++)
                {
                    auto holderBalanceAfter = balanceHelper.loadBalance(
                            holdersAmounts[i].account.key.getPublicKey(),
                            assetCode);
                    uint64_t receivedAmount = 0;
                    REQUIRE(bigDivide(receivedAmount, maxPayoutAmount,
                                      holdersAmounts[i].amount,
                                      assetFrame->getIssued(), ROUND_DOWN));

                    REQUIRE(holderBalanceAfter->getAmount() ==
                            holdersBalancesBefore[i]->getAmount() +
                            receivedAmount);
                    receiversCount++;
                }

                REQUIRE(receiversCount ==
                        result.success().payoutResponses.size());
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
            auto payAssetAmount = preIssuedAmount / 2;
            issuanceRequestHelper.createAssetWithPreIssuedAmount(thirdPartyIssuer,
                                 thirdPartyAssetCode, preIssuedAmount, root);
            manageAssetTestHelper.updateAsset(thirdPartyIssuer,
                              thirdPartyAssetCode, root,
                              static_cast<uint32_t>(AssetPolicy::TRANSFERABLE));
            auto thirdPartyIssuerBalance = balanceHelper.
                    loadBalance(thirdPartyIssuerID, thirdPartyAssetCode);
            REQUIRE(thirdPartyIssuerBalance != nullptr);
            reference = SecretKey::random().getStrKeyPublic();
            issuanceRequestHelper.applyCreateIssuanceRequest(thirdPartyIssuer,
                 thirdPartyAssetCode, payAssetAmount,
                 thirdPartyIssuerBalance->getBalanceID(), reference, &issuanceTasks);

            // create balance of third-party asset for owner
            manageBalanceTestHelper.createBalance(owner, ownerID, thirdPartyAssetCode);
            auto ownerThirdPartyBalance = balanceHelper.
                    loadBalance(ownerID, thirdPartyAssetCode);
            REQUIRE(ownerThirdPartyBalance != nullptr);
            auto ownerThirdPartyBalanceID = ownerThirdPartyBalance->getBalanceID();
            reference = SecretKey::random().getStrKeyPublic();
            issuanceRequestHelper.applyCreateIssuanceRequest(thirdPartyIssuer,
                 thirdPartyAssetCode, payAssetAmount, ownerThirdPartyBalanceID,
                 reference, &issuanceTasks);

            uint64_t maxPayoutAmount = payAssetAmount / 10;

            SECTION("Zero fee")
            {
                payoutTestHelper.applyPayoutTx(owner, assetCode,
                  ownerThirdPartyBalanceID, maxPayoutAmount, ONE, ONE, zeroFee);
            }

            SECTION("Non-zero fee")
            {
                auto feeEntry = setFeesTestHelper.createFeeEntry(
                        FeeType::PAYOUT_FEE, thirdPartyAssetCode, ONE, ONE);
                setFeesTestHelper.applySetFeesTx(root, &feeEntry, false);

                Fee fee;
                fee.fixed = feeEntry.fixedFee;
                REQUIRE(bigDivide(fee.percent, feeEntry.percentFee,
                                  maxPayoutAmount, 100 * ONE, ROUND_UP));

                payoutTestHelper.applyPayoutTx(owner, assetCode,
                   ownerThirdPartyBalanceID, maxPayoutAmount, ONE, ONE/10, fee);

                SECTION("Underfunded")
                {
                    payAssetAmount *= 100;
                    REQUIRE(bigDivide(fee.percent, feeEntry.percentFee,
                                      payAssetAmount, 100 * ONE, ROUND_UP));
                    payoutTestHelper.applyPayoutTx(owner, assetCode,
                               ownerThirdPartyBalanceID, payAssetAmount,
                               ONE, ONE/10, fee, PayoutResultCode::UNDERFUNDED);
                }

                SECTION("Limit exceeded")
                {
                    auto limitsOp = manageLimitsTestHelper.createManageLimitsOp(
                            thirdPartyAssetCode, StatsOpType::PAYMENT_OUT, true,
                            ONE, ONE, ONE, ONE);
                    manageLimitsTestHelper.applyManageLimitsTx(root, limitsOp);

                    payoutTestHelper.applyPayoutTx(owner, assetCode,
                           ownerThirdPartyBalanceID, maxPayoutAmount,
                           ONE, ONE/10, fee, PayoutResultCode::LIMITS_EXCEEDED);
                }
            }

            SECTION("Min amount too much")
            {
                payoutTestHelper.applyPayoutTx(owner, assetCode,
                       ownerThirdPartyBalanceID, maxPayoutAmount, 1000 * ONE,
                       ONE, zeroFee, PayoutResultCode::MIN_AMOUNT_TOO_BIG);
            }
        }
    }

    SECTION("Invalid amount")
    {
        payoutTestHelper.applyPayoutTx(owner, assetCode, ownerBalanceID, 0, 0,
                               0, zeroFee, PayoutResultCode::INVALID_AMOUNT);
    }

    SECTION("Invalid asset code") {
        payoutTestHelper.applyPayoutTx(owner, "", ownerBalanceID, 100 * ONE, 0,
                           0, zeroFee, PayoutResultCode::INVALID_ASSET);
    }

    SECTION("Asset not found")
    {
        payoutTestHelper.applyPayoutTx(owner, "USD", ownerBalanceID, 100 * ONE,
                           0, 0, zeroFee, PayoutResultCode::ASSET_NOT_FOUND);
    }

    SECTION("Asset not transferable")
    {
        AssetCode newAssetCode = "USD";
        manageAssetTestHelper.createAsset(owner, owner.key, newAssetCode, root, 0);
        manageBalanceTestHelper.createBalance(owner, ownerID, newAssetCode);
        auto newBalance = balanceHelper.loadBalance(ownerID, newAssetCode);
        REQUIRE(newBalance);
        payoutTestHelper.applyPayoutTx(owner, newAssetCode,
                           newBalance->getBalanceID(), 100 * ONE, 0, 0, zeroFee,
                           PayoutResultCode::ASSET_NOT_TRANSFERABLE);
    }

    SECTION("Balance not found")
    {
        auto account = SecretKey::random();
        payoutTestHelper.applyPayoutTx(owner, assetCode, account.getPublicKey(),
               100 * ONE, 0, 0, zeroFee, PayoutResultCode::BALANCE_NOT_FOUND);
    }

    SECTION("Balance account mismatched")
    {
        Account account = {SecretKey::random(), Salt(0)};
        auto accountID = account.key.getPublicKey();
        createAccountTestHelper.applyCreateAccountTx(root, accountID,
                                                     AccountType::SYNDICATE);
        manageBalanceTestHelper.createBalance(account, accountID, assetCode);
        auto accountBalance = balanceHelper.loadBalance(accountID, assetCode);
        payoutTestHelper.applyPayoutTx(owner, assetCode,
                   accountBalance->getBalanceID(), 100 * ONE, 0, 0, zeroFee,
                   PayoutResultCode::BALANCE_NOT_FOUND);
    }

    SECTION("Fee mismatched")
    {
        auto feeEntry = setFeesTestHelper.createFeeEntry(
                FeeType::PAYOUT_FEE, assetCode, 2*ONE, ONE);
        setFeesTestHelper.applySetFeesTx(root, &feeEntry, false);

        auto newAccount = Account{SecretKey::random(), Salt(0)};
        auto newAccountID = newAccount.key.getPublicKey();
        createAccountTestHelper.applyCreateAccountTx(root, newAccountID,
                                                     AccountType::GENERAL);
        manageBalanceTestHelper.createBalance(newAccount,
                                              newAccountID, assetCode);
        auto newBalance = balanceHelper.loadBalance(newAccountID, assetCode);
        REQUIRE(newBalance != nullptr);
        reference = SecretKey::random().getStrKeyPublic();
        issuanceRequestHelper.applyCreateIssuanceRequest(owner, assetCode,
           100*ONE, newBalance->getBalanceID(), reference, &issuanceTasks);

        SECTION("INSUFFICIENT_FEE_AMOUNT")
        {
            payoutTestHelper.applyPayoutTx(owner, assetCode, ownerBalanceID,
                                   100 * ONE, 0, 0, zeroFee,
                                   PayoutResultCode::INSUFFICIENT_FEE_AMOUNT);
        }

        SECTION("FEE_EXCEEDS_ACTUAL_AMOUNT")
        {
            Fee fee;
            fee.fixed = feeEntry.fixedFee;
            fee.percent = feeEntry.percentFee;
            payoutTestHelper.applyPayoutTx(owner, assetCode, ownerBalanceID,
                               ONE, 0, 0, fee,
                               PayoutResultCode::FEE_EXCEEDS_ACTUAL_AMOUNT);
        }

        SECTION("UNDERFUNDED")
        {
            Fee fee;
            fee.fixed = feeEntry.fixedFee;
            uint64_t payAmount = 9000*ONE;
            REQUIRE(bigDivide(fee.percent, feeEntry.percentFee,
                              payAmount, 100 * ONE, ROUND_UP));
            payoutTestHelper.applyPayoutTx(owner, assetCode, ownerBalanceID,
                                           payAmount, 0, 0, fee,
                                           PayoutResultCode::UNDERFUNDED);
        }
    }

    SECTION("Holders not found")
    {
        payoutTestHelper.applyPayoutTx(owner, assetCode, ownerBalanceID,
                100 * ONE, 0, 0, zeroFee, PayoutResultCode::HOLDERS_NOT_FOUND);
    }
}