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

TEST_CASE("manage asset pair", "[dep_tx][manage_asset_pair]")
{
    Config const& cfg = getTestConfig(0, Config::TESTDB_POSTGRESQL);
    VirtualClock clock;
    Application::pointer appPtr = Application::create(clock, cfg);
    Application& app = *appPtr;
    app.start();
	closeLedgerOn(app, 2, 1, 7, 2014);
	LedgerDelta delta(app.getLedgerManager().getCurrentLedgerHeader(),
		app.getDatabase());

    // set up world
    SecretKey root = getRoot();
	Salt rootSalt = 1;

	SECTION("Basic")
	{
		AssetCode base = "EUR";
		AssetCode quote = "UAH";
		int64_t physicalPrice = 12;
		int64_t physicalPriceCorrection = 95 * ONE;
		int64_t maxPriceStep = 5 * ONE;
		int32_t policies = getAnyAssetPairPolicy();
		SECTION("Invalid asset")
		{
			base = "''asd";
			applyManageAssetPairTx(app, root, rootSalt, base, quote, physicalPrice, physicalPriceCorrection, maxPriceStep, policies, MANAGE_ASSET_PAIR_CREATE, MANAGE_ASSET_PAIR_INVALID_ASSET);
			applyManageAssetPairTx(app, root, rootSalt, quote, base, physicalPrice, physicalPriceCorrection, maxPriceStep, policies, MANAGE_ASSET_PAIR_CREATE, MANAGE_ASSET_PAIR_INVALID_ASSET);
		}
		SECTION("Invalid action")
		{
			applyManageAssetPairTx(app, root, rootSalt, quote, base, physicalPrice, physicalPriceCorrection, maxPriceStep, policies, ManageAssetPairAction(1201), MANAGE_ASSET_PAIR_INVALID_ACTION);
		}
		SECTION("Invalid physical price")
		{
			applyManageAssetPairTx(app, root, rootSalt, quote, base, -physicalPrice, physicalPriceCorrection, maxPriceStep, policies, MANAGE_ASSET_PAIR_CREATE, MANAGE_ASSET_PAIR_MALFORMED);
		}
		SECTION("Invalid physical price correction")
		{
			applyManageAssetPairTx(app, root, rootSalt, quote, base, physicalPrice, -physicalPriceCorrection, maxPriceStep, policies, MANAGE_ASSET_PAIR_CREATE, MANAGE_ASSET_PAIR_MALFORMED);
		}
		SECTION("Invalid max price step")
		{
			applyManageAssetPairTx(app, root, rootSalt, quote, base, physicalPrice, physicalPriceCorrection, -maxPriceStep, policies, MANAGE_ASSET_PAIR_CREATE, MANAGE_ASSET_PAIR_MALFORMED);
		}
		SECTION("Invalid policies")
		{
			applyManageAssetPairTx(app, root, rootSalt, quote, base, physicalPrice, physicalPriceCorrection, maxPriceStep, -policies, MANAGE_ASSET_PAIR_CREATE, MANAGE_ASSET_PAIR_INVALID_POLICIES);
		}
		SECTION("Asset does not exists")
		{
			applyManageAssetPairTx(app, root, rootSalt, quote, base, physicalPrice, physicalPriceCorrection, maxPriceStep, policies, MANAGE_ASSET_PAIR_CREATE, MANAGE_ASSET_PAIR_ASSET_NOT_FOUND);
		}
		SECTION("Asset created")
		{
			applyManageAssetTx(app, root, rootSalt, base);
			applyManageAssetTx(app, root, rootSalt, quote);
			SECTION("Pair already exists")
			{
				applyManageAssetPairTx(app, root, rootSalt, quote, base, physicalPrice, physicalPriceCorrection, maxPriceStep, policies);
				applyManageAssetPairTx(app, root, rootSalt, quote, base, physicalPrice, physicalPriceCorrection, maxPriceStep, policies, MANAGE_ASSET_PAIR_CREATE, MANAGE_ASSET_PAIR_ALREADY_EXISTS);
				// reverse pair already exists
				applyManageAssetPairTx(app, root, rootSalt, base, quote, physicalPrice, physicalPriceCorrection, maxPriceStep, policies, MANAGE_ASSET_PAIR_CREATE, MANAGE_ASSET_PAIR_ALREADY_EXISTS);
			}
			SECTION("Pair does not exists")
			{
				applyManageAssetPairTx(app, root, rootSalt, quote, base, physicalPrice, physicalPriceCorrection, maxPriceStep, policies, MANAGE_ASSET_PAIR_UPDATE_POLICIES, MANAGE_ASSET_PAIR_NOT_FOUND);
				applyManageAssetPairTx(app, root, rootSalt, quote, base, physicalPrice, physicalPriceCorrection, maxPriceStep, policies, MANAGE_ASSET_PAIR_UPDATE_PRICE, MANAGE_ASSET_PAIR_NOT_FOUND);
			}
			SECTION("Create -> update policies -> update price")
			{
				// create
				applyManageAssetPairTx(app, root, rootSalt, quote, base, physicalPrice, physicalPriceCorrection, maxPriceStep, policies, MANAGE_ASSET_PAIR_CREATE);
				// update policies
				physicalPriceCorrection = physicalPriceCorrection + 100 * ONE;
				maxPriceStep = maxPriceStep + 1 * ONE;
				policies = ASSET_PAIR_TRADEABLE | ASSET_PAIR_CURRENT_PRICE_RESTRICTION;
				applyManageAssetPairTx(app, root, rootSalt, quote, base, physicalPrice, physicalPriceCorrection, maxPriceStep, policies, MANAGE_ASSET_PAIR_UPDATE_POLICIES);
				// update price
				physicalPrice = physicalPrice + 125 * ONE;
				applyManageAssetPairTx(app, root, rootSalt, quote, base, physicalPrice, physicalPriceCorrection, maxPriceStep, policies, MANAGE_ASSET_PAIR_UPDATE_PRICE);
			}
		}
	}
}
