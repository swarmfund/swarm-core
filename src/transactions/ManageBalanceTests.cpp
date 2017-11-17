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

    // set up world
    SecretKey rootKP = getRoot();
	Salt rootSeq = 1;
    closeLedgerOn(app, 2, 1, 7, 2014);

    auto account = SecretKey::random();
    auto account2 = SecretKey::random();
    auto balanceID = SecretKey::random().getPublicKey();
    auto balance2ID = SecretKey::random().getPublicKey();
    applyCreateAccountTx(app, rootKP, account, rootSeq++, GENERAL);
	applyCreateAccountTx(app, rootKP, account2, rootSeq++, GENERAL);
    
    auto accSeq = 1;
    auto acc2Seq = 1;
    AssetCode asset = "AETH";
    AssetCode asset2 = "AETC";
    applyManageAssetTx(app, rootKP, rootSeq++, asset);
    applyManageAssetTx(app, rootKP, rootSeq++, asset2);
    
    SECTION("Can create for account by himself")
    {
        applyManageBalanceTx(app, account, account, accSeq++, balanceID, asset, MANAGE_BALANCE_CREATE);
        SECTION("Can not delete base balance by himself")
        {
            applyManageBalanceTx(app, account, account, accSeq++, balanceID, asset, MANAGE_BALANCE_DELETE, MANAGE_BALANCE_MALFORMED);
        }
    }
    SECTION("Can not create  for non-existent asset ")
    {
        applyManageBalanceTx(app, account, account, accSeq++, balance2ID,
            "ABTC", MANAGE_BALANCE_CREATE, MANAGE_BALANCE_ASSET_NOT_FOUND);
    }
    SECTION("Can not create  for invalid asset ")
    {
        applyManageBalanceTx(app, account2, account2, accSeq++, balance2ID,
            "", MANAGE_BALANCE_CREATE, MANAGE_BALANCE_INVALID_ASSET);
    }
	SECTION("Can create balance")
	{
        applyManageBalanceTx(app, account2, account2, acc2Seq++, balanceID, asset);
        SECTION("Can't delete")
        {
            applyManageBalanceTx(app, account2, account2, accSeq++, balanceID, asset, MANAGE_BALANCE_DELETE, MANAGE_BALANCE_MALFORMED);
        }
        SECTION("Can not create for same balanceID twice ")
        {
            auto account3 = SecretKey::random();
            applyCreateAccountTx(app, rootKP, account3, rootSeq++, GENERAL);
            auto acc3Seq = 1;

            applyManageBalanceTx(app, account3, account3, acc3Seq++, balanceID, asset2, MANAGE_BALANCE_CREATE,
                MANAGE_BALANCE_ALREADY_EXISTS);
        }
    }
    SECTION("Can not create for non-existent account")
    {
        auto account3 = SecretKey::random();
        TransactionFramePtr txFrame = createManageBalanceTx(app.getNetworkID(), account2, account3, acc2Seq++, balanceID, asset2, MANAGE_BALANCE_CREATE);
        checkTransactionForOpResult(txFrame, app, OperationResultCode::opNO_COUNTERPARTY);
    }
}
