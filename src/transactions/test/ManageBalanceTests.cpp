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
#include "ledger/BalanceHelper.h"
#include "ledger/LedgerManager.h"
#include "ledger/LedgerDelta.h"
#include "test_helper/TestManager.h"
#include "test_helper/CreateAccountTestHelper.h"
#include "test_helper/ManageAssetTestHelper.h"
#include "test_helper/ManageBalanceTestHelper.h"

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

	LedgerDelta delta(app.getLedgerManager().getCurrentLedgerHeader(),
		app.getDatabase());

	auto root = Account{ getRoot(), Salt(0) };

	auto balanceHelper = BalanceHelper::Instance();
	ManageBalanceTestHelper manageBalanceTestHelper(testManager);

    auto account = Account{ SecretKey::random() , 0 };
    auto account2 = Account{ SecretKey::random(), 0 };
	CreateAccountTestHelper createAccountTestHelper(testManager);
	createAccountTestHelper.applyCreateAccountTx(root, account.key.getPublicKey(), AccountType::GENERAL);
	createAccountTestHelper.applyCreateAccountTx(root, account2.key.getPublicKey(), AccountType::GENERAL);
    
    AssetCode asset = "EUR";
	AssetCode asset2 = "USD";

	auto manageAssetHelper = ManageAssetTestHelper(testManager);
	auto preissuedSigner = SecretKey::random();
    manageAssetHelper.createAsset(root, preissuedSigner, asset, root, 1);
    
    SECTION("Can create for account by himself")
    {
		manageBalanceTestHelper.createBalance(account, account.key.getPublicKey(), asset);
        SECTION("Can not delete base balance by himself")
        {
			manageBalanceTestHelper.applyManageBalanceTx(account, account.key.getPublicKey(), asset, 
														 ManageBalanceAction::DELETE_BALANCE, 
														 ManageBalanceResultCode::MALFORMED);
        }
    }
    SECTION("Can not create for non-existent asset ")
    {
		manageBalanceTestHelper.applyManageBalanceTx(account, account.key.getPublicKey(), "ABTC", 
													 ManageBalanceAction::CREATE, 
													 ManageBalanceResultCode::ASSET_NOT_FOUND);
    }
    SECTION("Can not create for invalid asset ")
    {
		manageBalanceTestHelper.applyManageBalanceTx(account2, account2.key.getPublicKey(), "",
													 ManageBalanceAction::CREATE,
													 ManageBalanceResultCode::INVALID_ASSET);
    }
    SECTION("Can not create for non-existent account")
    {
		auto account3 = Account{ SecretKey::random(), Salt(0) };
		TransactionFramePtr txFrame = manageBalanceTestHelper.createManageBalanceTx(account2, account3.key.getPublicKey(), asset2, ManageBalanceAction::CREATE);
        checkTransactionForOpResult(txFrame, app, OperationResultCode::opNO_COUNTERPARTY);
    }
}
