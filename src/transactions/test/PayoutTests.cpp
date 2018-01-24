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

TEST_CASE("payout", "[tx][payout]") {
    using xdr::operator==;

    Config const &cfg = getTestConfig(0, Config::TESTDB_POSTGRESQL);
    VirtualClock clock;
    Application::pointer appPtr = Application::create(clock, cfg);
    Application &app = *appPtr;
    app.start();

    auto testManager = TestManager::make(app);
    Database &db = testManager->getDB();

    LedgerDelta delta(app.getLedgerManager().getCurrentLedgerHeader(),
                      app.getDatabase());

    upgradeToCurrentLedgerVersion(app);

    auto root = Account{getRoot(), Salt(0)};

    CreateAccountTestHelper createAccountTestHelper(testManager);
    IssuanceRequestHelper issuanceRequestHelper(testManager);
    ManageAssetTestHelper manageAssetTestHelper(testManager);
    ManageBalanceTestHelper manageBalanceTestHelper(testManager);
    PayoutTestHelper payoutTestHelper(testManager);
    ReviewAssetRequestHelper reviewAssetRequestHelper(testManager);
    ReviewIssuanceRequestHelper reviewIssuanceRequestHelper(testManager);


    // create issuer
    auto issuer = Account{SecretKey::random(), Salt(0)};
    auto issuerID = issuer.key.getPublicKey();
    createAccountTestHelper.applyCreateAccountTx(root, issuerID, AccountType::SYNDICATE);

    // create asset for payout
    AssetCode assetCode = "EUR";
    uint64_t preIssuedAmount = 1000 * ONE;
    uint64_t maxIssuanceAmount = UINT64_MAX / 2;
    SecretKey preIssuedSigner = SecretKey::random();
    auto transferableAssetPolicy = static_cast<uint32>(AssetPolicy::TRANSFERABLE);
    auto assetCreationRequest = manageAssetTestHelper.createAssetCreationRequest(assetCode,
                                                                                 preIssuedSigner.getPublicKey(),
                                                                                 "{}", maxIssuanceAmount,
                                                                                 transferableAssetPolicy,
                                                                                 preIssuedAmount);
    auto manageAssetResult = manageAssetTestHelper.applyManageAssetTx(issuer, 0, assetCreationRequest);
    reviewAssetRequestHelper.applyReviewRequestTx(root, manageAssetResult.success().requestID,
                                                  ReviewRequestOpAction::APPROVE, "");

    auto issuerBalance = BalanceHelper::Instance()->loadBalance(issuerID,
                                                                assetCode, db, nullptr);
    REQUIRE(issuerBalance);

    std::string reference = SecretKey::random().getStrKeyPublic();
    issuanceRequestHelper.applyCreateIssuanceRequest(issuer, assetCode, 698 * ONE,
                                                     issuerBalance->getBalanceID(), reference);

    //create fee
    Fee zeroFee;
    zeroFee.fixed = 0;
    zeroFee.percent = 0;

    SECTION("Happy path") {
        // create holders and give them some amount of asset

        // create ants (holders with balance amounts <= ONE)
        auto ant1 = Account{SecretKey::random(), Salt(0)};
        auto ant1ID = ant1.key.getPublicKey();
        createAccountTestHelper.applyCreateAccountTx(root, ant1ID, AccountType::GENERAL);
        manageBalanceTestHelper.createBalance(ant1, ant1ID, assetCode);
        auto ant1Balance = BalanceHelper::Instance()->loadBalance(ant1ID, assetCode, db, nullptr);
        reference = SecretKey::random().getStrKeyPublic();
        issuanceRequestHelper.applyCreateIssuanceRequest(issuer, assetCode, ONE / 10,
                                                         ant1Balance->getBalanceID(), reference);

        auto ant2 = Account{SecretKey::random(), Salt(0)};
        auto ant2ID = ant2.key.getPublicKey();
        createAccountTestHelper.applyCreateAccountTx(root, ant2ID, AccountType::GENERAL);
        manageBalanceTestHelper.createBalance(ant2, ant2ID, assetCode);
        auto ant2Balance = BalanceHelper::Instance()->loadBalance(ant2ID, assetCode, db, nullptr);
        reference = SecretKey::random().getStrKeyPublic();
        issuanceRequestHelper.applyCreateIssuanceRequest(issuer, assetCode, 9 * (ONE / 10),
                                                         ant2Balance->getBalanceID(), reference);

        auto ant3 = Account{SecretKey::random(), Salt(0)};
        auto ant3ID = ant3.key.getPublicKey();
        createAccountTestHelper.applyCreateAccountTx(root, ant3ID, AccountType::GENERAL);
        manageBalanceTestHelper.createBalance(ant3, ant3ID, assetCode);
        auto ant3Balance = BalanceHelper::Instance()->loadBalance(ant3ID, assetCode, db, nullptr);
        reference = SecretKey::random().getStrKeyPublic();
        issuanceRequestHelper.applyCreateIssuanceRequest(issuer, assetCode, ONE,
                                                         ant3Balance->getBalanceID(), reference);

        // create holders with balance amount > ONE
        auto holder1 = Account{SecretKey::random(), Salt(0)};
        auto holder1ID = holder1.key.getPublicKey();
        createAccountTestHelper.applyCreateAccountTx(root, holder1ID, AccountType::GENERAL);
        manageBalanceTestHelper.createBalance(holder1, holder1ID, assetCode);
        auto holder1Balance = BalanceHelper::Instance()->loadBalance(holder1ID, assetCode, db, nullptr);
        reference = SecretKey::random().getStrKeyPublic();
        issuanceRequestHelper.applyCreateIssuanceRequest(issuer, assetCode, 200 * ONE,
                                                         holder1Balance->getBalanceID(), reference);

        auto holder2 = Account{SecretKey::random(), Salt(0)};
        auto holder2ID = holder2.key.getPublicKey();
        createAccountTestHelper.applyCreateAccountTx(root, holder2ID, AccountType::GENERAL);
        manageBalanceTestHelper.createBalance(holder2, holder2ID, assetCode);
        auto holder2Balance = BalanceHelper::Instance()->loadBalance(holder2ID, assetCode, db, nullptr);
        reference = SecretKey::random().getStrKeyPublic();
        issuanceRequestHelper.applyCreateIssuanceRequest(issuer, assetCode, 100 * ONE,
                                                         holder2Balance->getBalanceID(), reference);

        SECTION("Pay with own asset") {
            SECTION("Zero fee") {
                payoutTestHelper.applyPayoutTx(issuer, assetCode, issuerBalance->getBalanceID(), 100 * ONE, zeroFee);

                issuerBalance = BalanceHelper::Instance()->loadBalance(issuerID, assetCode, db, nullptr);
                holder1Balance = BalanceHelper::Instance()->loadBalance(holder1ID, assetCode, db, nullptr);
                holder2Balance = BalanceHelper::Instance()->loadBalance(holder2ID, assetCode, db, nullptr);

                REQUIRE(issuerBalance->getAmount() == 668 * ONE);
                REQUIRE(holder1Balance->getAmount() == 220 * ONE);
                REQUIRE(holder2Balance->getAmount() == 110 * ONE);
            }

            SECTION("Non-zero fee") {
                auto payoutFeeFrame = FeeFrame::create(FeeType::PAYOUT_FEE, 20 * ONE,
                                                       int64_t(3 * ONE), assetCode,
                                                       &issuerID);
                auto payoutFee = payoutFeeFrame->getFee();
                applySetFees(app, root.key, root.salt, &payoutFee, false, nullptr);

                Fee fee;
                fee.fixed = 20 * ONE;
                fee.percent = 3 * ONE;


                payoutTestHelper.applyPayoutTx(issuer, assetCode, issuerBalance->getBalanceID(), 100 * ONE, fee);

                issuerBalance = BalanceHelper::Instance()->loadBalance(issuerID, assetCode, db, nullptr);
                auto commissionBalance = BalanceHelper::Instance()->loadBalance(getCommissionKP().getPublicKey(),
                                                                               assetCode, db, nullptr);
                REQUIRE(issuerBalance->getAmount() == 645 * ONE);
                REQUIRE(commissionBalance->getAmount() == 23 * ONE);
            }
        }

        SECTION("Pay with any third-party asset") {
            // create third-party issuer and his asset
            auto thirdPartyIssuer = Account {SecretKey::random(), Salt(0)};
            auto thirdPartyIssuerID = thirdPartyIssuer.key.getPublicKey();
            createAccountTestHelper.applyCreateAccountTx(root, thirdPartyIssuerID, AccountType::SYNDICATE);
            AssetCode thirdPartyAssetCode = "USD";
            preIssuedAmount = 500 * ONE;
            assetCreationRequest = manageAssetTestHelper.createAssetCreationRequest(thirdPartyAssetCode,
                                                                                    preIssuedSigner.getPublicKey(),
                                                                                    "{}", maxIssuanceAmount,
                                                                                    transferableAssetPolicy,
                                                                                    preIssuedAmount);
            manageAssetResult = manageAssetTestHelper.applyManageAssetTx(thirdPartyIssuer, 0, assetCreationRequest);
            reviewAssetRequestHelper.applyReviewRequestTx(root, manageAssetResult.success().requestID,
                                                          ReviewRequestOpAction::APPROVE, "");
            auto thirdPartyIssuerBalance = BalanceHelper::Instance()->loadBalance(thirdPartyIssuerID,
                                                                                  thirdPartyAssetCode,
                                                                                  db, nullptr);
            reference = SecretKey::random().getStrKeyPublic();
            issuanceRequestHelper.applyCreateIssuanceRequest(thirdPartyIssuer, thirdPartyAssetCode, 300 * ONE,
                                                             thirdPartyIssuerBalance->getBalanceID(), reference);

            // create balance of third-party asset for issuer
            manageBalanceTestHelper.createBalance(issuer, issuerID, thirdPartyAssetCode);
            auto issuerThirdPartyBalance = BalanceHelper::Instance()->loadBalance(issuerID, thirdPartyAssetCode,
                                                                                  db, nullptr);
            reference = SecretKey::random().getStrKeyPublic();
            issuanceRequestHelper.applyCreateIssuanceRequest(thirdPartyIssuer, thirdPartyAssetCode, 200 * ONE,
                                                             issuerThirdPartyBalance->getBalanceID(), reference);
            SECTION("Zero fee") {
                payoutTestHelper.applyPayoutTx(issuer, assetCode, issuerThirdPartyBalance->getBalanceID(), 100 * ONE,
                                               zeroFee);

                issuerThirdPartyBalance = BalanceHelper::Instance()->loadBalance(issuerID, thirdPartyAssetCode, db,
                                                                                 nullptr);
                auto holder1ThirdPartyBalance = BalanceHelper::Instance()->loadBalance(holder1ID, thirdPartyAssetCode, db,
                                                                                       nullptr);
                auto holder2ThirdPartyBalance = BalanceHelper::Instance()->loadBalance(holder2ID, thirdPartyAssetCode, db,
                                                                                       nullptr);

                REQUIRE(issuerThirdPartyBalance->getAmount() == 170 * ONE);
                REQUIRE(holder1ThirdPartyBalance->getAmount() == 20 * ONE);
                REQUIRE(holder2ThirdPartyBalance->getAmount() == 10 * ONE);
            }

            SECTION("Non-zero fee") {
                auto payoutFeeFrame = FeeFrame::create(FeeType::PAYOUT_FEE, 10 * ONE,
                                                       int64_t(5 * ONE), thirdPartyAssetCode,
                                                       &issuerID);
                auto payoutFee = payoutFeeFrame->getFee();
                applySetFees(app, root.key, root.salt, &payoutFee, false, nullptr);

                Fee fee;
                fee.fixed = 10 * ONE;
                fee.percent = 5 * ONE;

                payoutTestHelper.applyPayoutTx(issuer, assetCode, issuerThirdPartyBalance->getBalanceID(), 100 * ONE,
                                               fee);

                issuerThirdPartyBalance = BalanceHelper::Instance()->loadBalance(issuerID, thirdPartyAssetCode, db,
                                                                                 nullptr);
                auto commissionBalance = BalanceHelper::Instance()->loadBalance(getCommissionKP().getPublicKey(),
                                                                                thirdPartyAssetCode, db, nullptr);
                REQUIRE(issuerThirdPartyBalance->getAmount() == 155 * ONE);
                REQUIRE(commissionBalance->getAmount() == 15 * ONE);
            }
        }
    }

    SECTION("Invalid amount") {
        payoutTestHelper.applyPayoutTx(issuer, assetCode, issuerBalance->getBalanceID(), 0, zeroFee,
                                       PayoutResultCode::MALFORMED);
    }

    SECTION("Invalid asset code") {
        payoutTestHelper.applyPayoutTx(issuer, "", issuerBalance->getBalanceID(), 100 * ONE, zeroFee,
                                       PayoutResultCode::MALFORMED);
    }

    SECTION("Asset not found") {
        payoutTestHelper.applyPayoutTx(issuer, "USD", issuerBalance->getBalanceID(), 100 * ONE, zeroFee,
                                       PayoutResultCode::ASSET_NOT_FOUND);
    }

    SECTION("Balance not found") {
        auto account = SecretKey::random();
        payoutTestHelper.applyPayoutTx(issuer, assetCode, account.getPublicKey(), 100 * ONE, zeroFee,
                                       PayoutResultCode::BALANCE_NOT_FOUND);
    }

    SECTION("Balance account mismatched") {
        Account account = {SecretKey::random(), Salt(0)};
        auto accountID = account.key.getPublicKey();
        createAccountTestHelper.applyCreateAccountTx(root, accountID, AccountType::SYNDICATE);
        manageBalanceTestHelper.createBalance(account, accountID, assetCode);
        auto accountBalance = BalanceHelper::Instance()->loadBalance(accountID, assetCode, db, nullptr);
        payoutTestHelper.applyPayoutTx(issuer, assetCode, accountBalance->getBalanceID(), 100 * ONE, zeroFee,
                                       PayoutResultCode::BALANCE_ACCOUNT_MISMATCHED);
    }

    SECTION("Fee mismatched") {
        Fee giantFee;
        giantFee.fixed = preIssuedAmount;
        giantFee.percent = 0;
        payoutTestHelper.applyPayoutTx(issuer, assetCode, issuerBalance->getBalanceID(), 100 * ONE,
                                       giantFee, PayoutResultCode::FEE_MISMATCHED);
    }

    SECTION("Holders not found") {
        payoutTestHelper.applyPayoutTx(issuer, assetCode, issuerBalance->getBalanceID(), 100 * ONE, zeroFee,
                                       PayoutResultCode::HOLDERS_NOT_FOUND);
    }


}