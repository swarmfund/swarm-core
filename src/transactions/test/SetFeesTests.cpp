// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the ISC License. See the COPYING file at the top-level directory of
// this distribution or at http://opensource.org/licenses/ISC

#include "main/Application.h"
#include "overlay/LoopbackPeer.h"
#include "util/make_unique.h"
#include "main/test.h"
#include "TxTests.h"
#include "ledger/LedgerDeltaImpl.h"
#include "ledger/FeeHelper.h"
#include "transactions/SetFeesOpFrame.h"
#include "crypto/SHA.h"
#include "test_helper/CreateAccountTestHelper.h"
#include "test_helper/ManageAssetTestHelper.h"
#include "test_helper/ManageAssetPairTestHelper.h"
#include "test_helper/SetFeesTestHelper.h"
#include "test/test_marshaler.h"
#include "ledger/AssetHelper.h"
#include "ledger/AccountHelper.h"

using namespace stellar;
using namespace stellar::txtest;

typedef std::unique_ptr<Application> appPtr;

TEST_CASE("Set fee", "[tx][set_fees]") {
    Config const &cfg = getTestConfig(0, Config::TESTDB_POSTGRESQL);

    VirtualClock clock;
    Application::pointer appPtr = Application::create(clock, cfg);
    Application &app = *appPtr;
    app.start();
    TestManager::upgradeToCurrentLedgerVersion(app);

    Database &db = app.getDatabase();

    auto testManager = TestManager::make(app);

    LedgerDeltaImpl delta(app.getLedgerManager().getCurrentLedgerHeader(),
                          app.getDatabase());

    // set up world
    auto master = Account{getRoot(), Salt(1)};

    auto feeHelper = FeeHelper::Instance();


    CreateAccountTestHelper createAccountTestHelper(testManager);
    ManageAssetTestHelper manageAssetTestHelper(testManager);
    ManageAssetPairTestHelper manageAssetPairTestHelper(testManager);
    SetFeesTestHelper setFeesTestHelper(testManager);

    AssetCode assetCode = "EURT";
    auto assetCreationRequest = manageAssetTestHelper.createAssetCreationRequest(assetCode, master.key.getPublicKey(),
                                                                                 "{}", UINT64_MAX,
                                                                                 uint32_t(AssetPolicy::BASE_ASSET),
                                                                                 1000000);
    manageAssetTestHelper.applyManageAssetTx(master, 0, assetCreationRequest);

    SECTION("Invalid asset") {
        auto fee = setFeesTestHelper.createFeeEntry(FeeType::PAYMENT_FEE, "", 0, 0);
        setFeesTestHelper.applySetFeesTx(master, &fee, false, SetFeesResultCode::INVALID_ASSET);
    }
    SECTION("Invalid fee type") {
        auto fee = setFeesTestHelper.createFeeEntry(FeeType(123), assetCode, 0, 0);
        setFeesTestHelper.applySetFeesTx(master, &fee, false, SetFeesResultCode::INVALID_FEE_TYPE);
    }
    SECTION("Asset not found") {
        auto fee = setFeesTestHelper.createFeeEntry(FeeType::PAYMENT_FEE, "AETH", 0, 0);
        setFeesTestHelper.applySetFeesTx(master, &fee, false, SetFeesResultCode::ASSET_NOT_FOUND);
    }

    // create account for further tests
    auto account = SecretKey::random();
    createAccountTestHelper.applyCreateAccountTx(master, account.getPublicKey(), AccountType::GENERAL);
    auto accountFrame = AccountHelper::Instance()->loadAccount(account.getPublicKey(), db);

    auto accountFee = feeHelper->loadForAccount(FeeType::PAYMENT_FEE, assetCode, FeeFrame::SUBTYPE_ANY,
                                                accountFrame, 0, db);
    REQUIRE(!accountFee);

    SECTION("AccountID is set") {
        auto feeEntry = setFeesTestHelper.createFeeEntry(FeeType::PAYMENT_FEE, assetCode, 1, 2);
        setFeesTestHelper.applySetFeesTx(master, &feeEntry, false);
        accountFee = feeHelper->loadForAccount(FeeType::PAYMENT_FEE, assetCode, FeeFrame::SUBTYPE_ANY,
                                               accountFrame, 0, db);
        REQUIRE(accountFee->getFee() == feeEntry);
    }
    SECTION("AccountType is set") {
        auto accountType = accountFrame->getAccountType();
        auto feeEntry = setFeesTestHelper.createFeeEntry(FeeType::PAYMENT_FEE, assetCode, 10, 20, nullptr,
                                                         &accountType);
        setFeesTestHelper.applySetFeesTx(master, &feeEntry, false);
        accountFee = feeHelper->loadForAccount(FeeType::PAYMENT_FEE, assetCode, FeeFrame::SUBTYPE_ANY,
                                               accountFrame, 0, db);
        REQUIRE(accountFee->getFee() == feeEntry);
    }

    SECTION("Both cannot be set") {
        auto accountID = accountFrame->getID();
        auto accountType = accountFrame->getAccountType();
        auto feeEntry = setFeesTestHelper.createFeeEntry(FeeType::PAYMENT_FEE, assetCode, 0, 0, &accountID,
                                                         &accountType);
        setFeesTestHelper.applySetFeesTx(master, &feeEntry, false, SetFeesResultCode::MALFORMED);
    }

    SECTION("Payment fee") {
        SECTION("Success") {
            auto fee = setFeesTestHelper.createFeeEntry(FeeType::PAYMENT_FEE, assetCode, 1, 2 * ONE);
            setFeesTestHelper.applySetFeesTx(master, &fee, false);
        }
        SECTION("Success zero") {
            auto fee = setFeesTestHelper.createFeeEntry(FeeType::PAYMENT_FEE, assetCode, 0, 0);
            setFeesTestHelper.applySetFeesTx(master, &fee, false);
        }
        SECTION("Invalid percent fee fee") {
            auto fee = setFeesTestHelper.createFeeEntry(FeeType::PAYMENT_FEE, assetCode, 0, 101 * ONE);
            setFeesTestHelper.applySetFeesTx(master, &fee, false, SetFeesResultCode::INVALID_AMOUNT);
        }
    }
    SECTION("Offer fee") {
        SECTION("Success") {
            auto fee = setFeesTestHelper.createFeeEntry(FeeType::OFFER_FEE, assetCode, 0, 2 * ONE);
            setFeesTestHelper.applySetFeesTx(master, &fee, false);
        }
        SECTION("Success zero") {
            auto fee = setFeesTestHelper.createFeeEntry(FeeType::OFFER_FEE, assetCode, 0, 0);
            setFeesTestHelper.applySetFeesTx(master, &fee, false);
        }
        SECTION("Invalid fixed fee") {
            auto fee = setFeesTestHelper.createFeeEntry(FeeType::OFFER_FEE, assetCode, 1, 2 * ONE);
            setFeesTestHelper.applySetFeesTx(master, &fee, false, SetFeesResultCode::INVALID_AMOUNT);
        }
        SECTION("Invalid percent fee") {
            auto fee = setFeesTestHelper.createFeeEntry(FeeType::OFFER_FEE, assetCode, 0, 101 * ONE);
            setFeesTestHelper.applySetFeesTx(master, &fee, false, SetFeesResultCode::INVALID_AMOUNT);
        }
    }
    SECTION("Withdrawal fee") {
        SECTION("Success") {
            auto fee = setFeesTestHelper.createFeeEntry(FeeType::WITHDRAWAL_FEE, assetCode, 1, 2 * ONE, nullptr,
                                                        nullptr, 1);
            setFeesTestHelper.applySetFeesTx(master, &fee, false);
            SECTION("update existing") {
                fee = setFeesTestHelper.createFeeEntry(FeeType::WITHDRAWAL_FEE, assetCode, 0, 0, nullptr, nullptr, 1);
                setFeesTestHelper.applySetFeesTx(master, &fee, false);
            }
            SECTION("Can delete") {
                fee = setFeesTestHelper.createFeeEntry(FeeType::WITHDRAWAL_FEE, assetCode, 0, 0, nullptr, nullptr, 1);
                setFeesTestHelper.applySetFeesTx(master, &fee, true);
            }

        }
        SECTION("Success zero") {
            auto fee = setFeesTestHelper.createFeeEntry(FeeType::WITHDRAWAL_FEE, assetCode, 0, 0);
            setFeesTestHelper.applySetFeesTx(master, &fee, false);
        }
        SECTION("Invalid percent fee") {
            auto fee = setFeesTestHelper.createFeeEntry(FeeType::WITHDRAWAL_FEE, assetCode, 0, 101 * ONE);
            setFeesTestHelper.applySetFeesTx(master, &fee, false, SetFeesResultCode::INVALID_AMOUNT);
        }
    }
    SECTION("Cross asset fee") {
        SECTION("Fee asset not allowed") {
            auto feeEntry = setFeesTestHelper.createFeeEntry(FeeType::WITHDRAWAL_FEE, assetCode, 0, 0, nullptr, nullptr,
                                                             FeeFrame::SUBTYPE_ANY, 0, INT64_MAX, &assetCode);
            setFeesTestHelper.applySetFeesTx(master, &feeEntry, false, SetFeesResultCode::FEE_ASSET_NOT_ALLOWED);
        }
        SECTION("Invalid fee asset") {
            AssetCode invalidAssetCode = "";
            auto feeEntry = setFeesTestHelper.createFeeEntry(FeeType::PAYMENT_FEE, assetCode, 0, 0, nullptr, nullptr,
                                                             FeeFrame::SUBTYPE_ANY, 0, INT64_MAX, &invalidAssetCode);
            setFeesTestHelper.applySetFeesTx(master, &feeEntry, false, SetFeesResultCode::INVALID_FEE_ASSET);
        }
        SECTION("Fee asset not found") {
            AssetCode code = "AETH";
            auto feeEntry = setFeesTestHelper.createFeeEntry(FeeType::PAYMENT_FEE, assetCode, 0, 0, nullptr, nullptr,
                                                             static_cast<int64_t>(PaymentFeeType::OUTGOING), 0,
                                                             INT64_MAX, &code);
            setFeesTestHelper.applySetFeesTx(master, &feeEntry, false, SetFeesResultCode::FEE_ASSET_NOT_FOUND);
        }
        SECTION("Cross asset not allowed for incoming payment") {
            AssetCode code = "AETH";
            auto feeEntry = setFeesTestHelper.createFeeEntry(FeeType::PAYMENT_FEE, assetCode, 0, 0, nullptr, nullptr,
                                                             static_cast<int64_t>(PaymentFeeType::INCOMING), 0,
                                                             INT64_MAX, &code);
            setFeesTestHelper.applySetFeesTx(master, &feeEntry, false, SetFeesResultCode::CROSS_ASSET_FEE_NOT_ALLOWED);
        }

        // create additional asset for further tests
        AssetCode usdt = "USDT";
        assetCreationRequest = manageAssetTestHelper.createAssetCreationRequest(usdt, master.key.getPublicKey(),
                                                                                "{}", UINT64_MAX,
                                                                                uint32_t(AssetPolicy::BASE_ASSET),
                                                                                1000000);
        manageAssetTestHelper.applyManageAssetTx(master, 0, assetCreationRequest);

        SECTION("Asset pair not found") {
            auto feeEntry = setFeesTestHelper.createFeeEntry(FeeType::PAYMENT_FEE, assetCode, 0, 0, nullptr, nullptr,
                                                             static_cast<int64_t>(PaymentFeeType::OUTGOING), 0,
                                                             INT64_MAX, &usdt);
            setFeesTestHelper.applySetFeesTx(master, &feeEntry, false, SetFeesResultCode::ASSET_PAIR_NOT_FOUND);
        }
        SECTION("Fee asset is the same as payment asset") {
            auto feeEntry = setFeesTestHelper.createFeeEntry(FeeType::PAYMENT_FEE, assetCode, 0, 0, nullptr, nullptr,
                                                             static_cast<int64_t>(PaymentFeeType::OUTGOING), 0,
                                                             INT64_MAX, &assetCode);
            setFeesTestHelper.applySetFeesTx(master, &feeEntry, false);

            SECTION("Delete existing fee") {
                setFeesTestHelper.applySetFeesTx(master, &feeEntry, true);
            }
        }
        SECTION("Invalid asset pair price") {
            manageAssetPairTestHelper.createAssetPair(master, assetCode, usdt, 0);
            auto feeEntry = setFeesTestHelper.createFeeEntry(FeeType::PAYMENT_FEE, assetCode, 0, 0, nullptr, nullptr,
                                                             static_cast<int64_t>(PaymentFeeType::OUTGOING), 0,
                                                             INT64_MAX, &usdt);
            setFeesTestHelper.applySetFeesTx(master, &feeEntry, false, SetFeesResultCode::INVALID_ASSET_PAIR_PRICE);
        }
        SECTION("Cross asset fee creation success") {
            manageAssetPairTestHelper.createAssetPair(master, assetCode, usdt, 5);
            auto feeEntry = setFeesTestHelper.createFeeEntry(FeeType::PAYMENT_FEE, assetCode, 0, 0, nullptr, nullptr,
                                                             static_cast<int64_t>(PaymentFeeType::OUTGOING), 0,
                                                             INT64_MAX, &usdt);
            setFeesTestHelper.applySetFeesTx(master, &feeEntry, false);
        }
    }
}
