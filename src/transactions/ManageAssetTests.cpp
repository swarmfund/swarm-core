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

TEST_CASE("manage asset", "[tx][manage_asset]")
{
    Config const& cfg = getTestConfig(0, Config::TESTDB_POSTGRESQL);
    VirtualClock clock;
    Application::pointer appPtr = Application::create(clock, cfg);
    Application& app = *appPtr;
    app.start();
    closeLedgerOn(app, 2, 1, 7, 2014);
	upgradeToCurrentLedgerVersion(app);
	LedgerDelta delta(app.getLedgerManager().getCurrentLedgerHeader(),
		app.getDatabase());

    // set up world
    SecretKey root = getRoot();
	Salt rootSeq = 1;

    auto account = SecretKey::random();
    auto balanceID = SecretKey::random().getPublicKey();
    applyCreateAccountTx(app, root, account, rootSeq++, GENERAL);
    auto accSeq = 1;
    auto exchangeSeq = 1;
    AssetCode code = "AETH";

	auto& db = app.getDatabase();
    SECTION("Malformed")
    {
        applyManageAssetTx(app, root, rootSeq++, code, -1,
            MANAGE_ASSET_CREATE, MANAGE_ASSET_MALFORMED);
    }
	SECTION("Can create asset")
	{
        applyManageAssetTx(app, root, rootSeq++, code, getAnyAssetPolicy());
        SECTION("Can update policies")
        {
            applyManageAssetTx(app, root, rootSeq++, code, 0,
                MANAGE_ASSET_UPDATE_POLICIES);
        }
        SECTION("Can not create same asset twice")
        {
            applyManageAssetTx(app, root, rootSeq++, code, getAnyAssetPolicy(),
                MANAGE_ASSET_CREATE, MANAGE_ASSET_ALREADY_EXISTS);
        }

    }
    SECTION("Can not update if not present")
    {
        applyManageAssetTx(app, root, rootSeq++, code, getAnyAssetPolicy(),
			MANAGE_ASSET_UPDATE_POLICIES, MANAGE_ASSET_NOT_FOUND);
    }
}
