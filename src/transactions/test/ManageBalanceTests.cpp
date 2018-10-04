// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0
#include "main/Application.h"
#include "util/Timer.h"
#include "main/Config.h"
#include "overlay/LoopbackPeer.h"
#include "main/test.h"
#include "TxTests.h"
#include "ledger/BalanceHelperLegacy.h"
#include "ledger/LedgerDeltaImpl.h"
#include "test_helper/TestManager.h"
#include "test_helper/CreateAccountTestHelper.h"
#include "test_helper/ManageAssetTestHelper.h"
#include "test_helper/ManageBalanceTestHelper.h"
#include "test/test_marshaler.h"

using namespace stellar;
using namespace stellar::txtest;

typedef std::unique_ptr<Application> appPtr;


TEST_CASE("manage balance", "[tx][manage_balance]")
{
    Config const& cfg = getTestConfig(0, Config::TESTDB_POSTGRESQL);
    VirtualClock clock;
    auto const appPtr = Application::create(clock, cfg);
    auto& app = *appPtr;
    app.start();
    auto testManager = TestManager::make(app);
    TestManager::upgradeToCurrentLedgerVersion(app);
    LedgerDeltaImpl delta(app.getLedgerManager().getCurrentLedgerHeader(),
                          app.getDatabase());

    auto root = Account{getRoot(), Salt(0)};

    auto balanceHelper = BalanceHelperLegacy::Instance();
    ManageBalanceTestHelper manageBalanceTestHelper(testManager);

    auto account = Account{SecretKey::random() , 0};
    auto account2 = Account{SecretKey::random(), 0};
    CreateAccountTestHelper createAccountTestHelper(testManager);
    createAccountTestHelper.
        applyCreateAccountTx(root, account.key.getPublicKey(),
                             AccountType::GENERAL);
    createAccountTestHelper.
        applyCreateAccountTx(root, account2.key.getPublicKey(),
                             AccountType::GENERAL);

    AssetCode asset = "EUR";
    AssetCode asset2 = "USD";

    auto manageAssetHelper = ManageAssetTestHelper(testManager);
    auto preissuedSigner = SecretKey::random();
    manageAssetHelper.createAsset(root, preissuedSigner, asset, root, 1);

    SECTION("Can create for account by himself")
    {
        auto accountID = account.key.getPublicKey();
        manageBalanceTestHelper.createBalance(account, accountID, asset);
        SECTION("Can not delete base balance by himself")
        {
            manageBalanceTestHelper.applyManageBalanceTx(account, accountID,
                                                         asset,
                                                         ManageBalanceAction::
                                                         DELETE_BALANCE,
                                                         ManageBalanceResultCode
                                                         ::MALFORMED);
        }
    }
    SECTION("Can not create for non-existent asset ")
    {
        auto accountID = account.key.getPublicKey();
        manageBalanceTestHelper.applyManageBalanceTx(account, accountID, "ABTC",
                                                     ManageBalanceAction::
                                                     CREATE,
                                                     ManageBalanceResultCode::
                                                     ASSET_NOT_FOUND);
    }
    SECTION("Can not create for invalid asset ")
    {
        auto accountID = account2.key.getPublicKey();
        manageBalanceTestHelper.applyManageBalanceTx(account2, accountID, "",
                                                     ManageBalanceAction::
                                                     CREATE,
                                                     ManageBalanceResultCode::
                                                     INVALID_ASSET);
    }
    SECTION("Can not create for non-existent account")
    {
        auto account3 = Account{SecretKey::random(), Salt(0)};
        auto accountID = account3.key.getPublicKey();
        TransactionFramePtr txFrame = manageBalanceTestHelper.
            createManageBalanceTx(account2, accountID, asset2,
                                  ManageBalanceAction::CREATE);
        checkTransactionForOpResult(txFrame, app,
                                    OperationResultCode::opNO_COUNTERPARTY);
    }
    SECTION("Can create unique")
    {
        account = Account{ SecretKey::random() , 0 };
        createAccountTestHelper.
            applyCreateAccountTx(root, account.key.getPublicKey(),
                AccountType::GENERAL);
        auto accountID = account.key.getPublicKey();
        manageBalanceTestHelper.applyManageBalanceTx(root, accountID, asset,
            ManageBalanceAction::CREATE_UNIQUE);
        manageBalanceTestHelper.applyManageBalanceTx(root, accountID, asset,
            ManageBalanceAction::CREATE_UNIQUE, ManageBalanceResultCode::BALANCE_ALREADY_EXISTS);
    }
}
