// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0
#include "main/Application.h"
#include "util/Timer.h"
#include "main/Config.h"
#include "overlay/LoopbackPeer.h"
#include "main/test.h"
#include "lib/catch.hpp"
#include "TxTests.h"
#include "test_helper/TestManager.h"
#include "transactions/test/test_helper/ManageAssetTestHelper.h"
#include "test_helper/ReviewAssetRequestHelper.h"
#include "test_helper/IssuanceRequestHelper.h"
#include "test_helper/ReviewPreIssuanceRequestHelper.h"
#include "ledger/LedgerManager.h"
#include "ledger/LedgerDelta.h"

using namespace stellar;
using namespace stellar::txtest;

typedef std::unique_ptr<Application> appPtr;

TEST_CASE("manage balance", "[tx][manage_balance]")
{
    Config const& cfg = getTestConfig(0, Config::TESTDB_POSTGRESQL);
    VirtualClock clock;
    Application::pointer appPtr = Application::create(clock, cfg);
    Application& app = *appPtr;
    app.start();
	LedgerDelta delta(app.getLedgerManager().getCurrentLedgerHeader(),
		app.getDatabase());

	auto root = Account{ getRoot(), Salt(0) };
    closeLedgerOn(app, 2, 1, 7, 2014);

	auto account = Account{ SecretKey::random(), Salt(0) };
	applyCreateAccountTx(app, root.key, account.key, root.salt, AccountType::GENERAL);
	auto account2 = Account{ SecretKey::random(), Salt(0) };
	applyCreateAccountTx(app, root.key, account2.key, root.salt, AccountType::GENERAL);
    
    AssetCode asset = "EUR";
	AssetCode asset2 = "USD";
	auto testManager = TestManager::make(app);
	auto manageAssetHelper = ManageAssetTestHelper(testManager);
	auto preissuedSigner = SecretKey::random();
    manageAssetHelper.createAsset(root, preissuedSigner, asset, root);
    
    SECTION("Can create for account by himself")
    {
        applyManageBalanceTx(app, account.key, account.key, account.salt, asset, ManageBalanceAction::CREATE);
        SECTION("Can not delete base balance by himself")
        {
            applyManageBalanceTx(app, account.key, account.key, account.salt, asset, ManageBalanceAction::DELETE_BALANCE, ManageBalanceResultCode::MALFORMED);
        }
    }
    SECTION("Can not create  for non-existent asset ")
    {
        applyManageBalanceTx(app, account.key, account.key, account.salt++, 
            "ABTC", ManageBalanceAction::CREATE, ManageBalanceResultCode::ASSET_NOT_FOUND);
    }
    SECTION("Can not create  for invalid asset ")
    {
        applyManageBalanceTx(app, account2.key, account2.key, account.salt++,
            "", ManageBalanceAction::CREATE, ManageBalanceResultCode::INVALID_ASSET);
    }
	SECTION("Can create balance")
	{
        applyManageBalanceTx(app, account2.key, account2.key, account2.salt++, asset);
        SECTION("Can't delete")
        {
            applyManageBalanceTx(app, account2.key, account2.key, account.salt++, asset, ManageBalanceAction::DELETE_BALANCE, ManageBalanceResultCode::MALFORMED);
        }
    }
    SECTION("Can not create for non-existent account")
    {
        auto account3 = SecretKey::random();
        TransactionFramePtr txFrame = createManageBalanceTx(app.getNetworkID(), account2.key, account3, account2.salt++, asset2, ManageBalanceAction::CREATE);
        checkTransactionForOpResult(txFrame, app, OperationResultCode::opNO_COUNTERPARTY);
    }
}
