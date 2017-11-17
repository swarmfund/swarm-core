// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0
#include "main/Application.h"
#include "ledger/LedgerManager.h"
#include "main/Config.h"
#include "overlay/LoopbackPeer.h"
#include "util/make_unique.h"
#include "main/test.h"
#include "lib/catch.hpp"
#include "util/Logging.h"
#include "TxTests.h"
#include "util/Timer.h"
#include "database/Database.h"
#include "ledger/LedgerDelta.h"
#include "crypto/Hex.h"
#include "crypto/SHA.h"

using namespace stellar;
using namespace stellar::txtest;

typedef std::unique_ptr<Application> appPtr;

TEST_CASE("Manage coins emission request", "[tx][manage_emission_request]")
{
    Config const& cfg = getTestConfig(0, Config::TESTDB_POSTGRESQL);

    VirtualClock clock;
    Application::pointer appPtr = Application::create(clock, cfg);
    Application& app = *appPtr;
    app.start();
    closeLedgerOn(app, 2, 1, 7, 2014);

	upgradeToCurrentLedgerVersion(app);

	int64 amount = app.getConfig().EMISSION_UNIT;

    // set up world
    SecretKey root = getRoot();
	Salt rootSeq = 1;
    auto asset = app.getBaseAsset();

	auto& db = app.getDatabase();

	auto account = SecretKey::random();
	applyCreateAccountTx(app, root, account, rootSeq++, GENERAL);

	SECTION("Invalid amount")
	{
		SECTION("Negative amount")
		{
			applyCoinsEmissionRequest(app, root, rootSeq++, account.getPublicKey(), -1, 0,
                asset, "1",  MANAGE_COINS_EMISSION_REQUEST_CREATE,
                MANAGE_COINS_EMISSION_REQUEST_INVALID_AMOUNT);
		}
		SECTION("Zero amount for new request")
		{
			applyCoinsEmissionRequest(app, root, rootSeq++, account.getPublicKey(), 0,
                0, asset, "1", MANAGE_COINS_EMISSION_REQUEST_CREATE,
				MANAGE_COINS_EMISSION_REQUEST_INVALID_AMOUNT);
		}

		SECTION("Non-dividable for emission unit")
		{
			applyCoinsEmissionRequest(app, root, rootSeq++, account.getPublicKey(),
                amount + 1, 0, asset, "1", MANAGE_COINS_EMISSION_REQUEST_CREATE);
		}

	}
	SECTION("Invalid emission id")
	{
		applyCoinsEmissionRequest(app, root, rootSeq++, account.getPublicKey(),
            amount, 1, asset, "1", MANAGE_COINS_EMISSION_REQUEST_CREATE,
            MANAGE_COINS_EMISSION_REQUEST_INVALID_REQUEST_ID);
	}
	SECTION("Invalid asset")
	{
		applyCoinsEmissionRequest(app, root, rootSeq++, account.getPublicKey(), amount, 0,
            "", "1",MANAGE_COINS_EMISSION_REQUEST_CREATE,
            MANAGE_COINS_EMISSION_REQUEST_INVALID_ASSET);
	}

    SECTION("Asset not found")
	{
		applyCoinsEmissionRequest(app, root, rootSeq++, account.getPublicKey(), amount, 0,
            "ABTC", "1",MANAGE_COINS_EMISSION_REQUEST_CREATE,
            MANAGE_COINS_EMISSION_REQUEST_ASSET_NOT_FOUND);
	}

    SECTION("Invalid ref")
	{
		applyCoinsEmissionRequest(app, root, rootSeq++, account.getPublicKey(), amount, 0,
            app.getBaseAsset(), "",MANAGE_COINS_EMISSION_REQUEST_CREATE,
            MANAGE_COINS_EMISSION_REQUEST_INVALID_REFERENCE);
	}


    SECTION("Asset mismatch")
	{
        auto code = "ABTC";
        applyManageAssetTx(app, root, rootSeq++, code);
		applyCoinsEmissionRequest(app, root, rootSeq++, account.getPublicKey(), amount, 0,
            code, "1",MANAGE_COINS_EMISSION_REQUEST_CREATE,
            MANAGE_COINS_EMISSION_REQUEST_ASSET_MISMATCH);
	}

	SECTION("Cant create request to master account")
	{
		auto coinsEmissionRequest = createCoinsEmissionRequest(app.getNetworkID(), root, rootSeq++, root.getPublicKey(), 0, amount, asset, "ref");
		LedgerDelta delta(app.getLedgerManager().getCurrentLedgerHeader(),
			app.getDatabase());
		applyCheck(coinsEmissionRequest, delta, app);

		REQUIRE(coinsEmissionRequest->getResult().result.results()[0].code() == opCOUNTERPARTY_WRONG_TYPE);
		
	}
	SECTION("Can create request")
	{
		auto requestID = applyCoinsEmissionRequest(app, root, rootSeq++,
            account.getPublicKey(), amount, 0, asset, "ref");
		SECTION("Can delete")
		{
			applyCoinsEmissionRequest(app, root, rootSeq++, account.getPublicKey(),
                amount, requestID, asset, "ref", 
                MANAGE_COINS_EMISSION_REQUEST_DELETE);
			std::vector<CoinsEmissionRequestFrame::pointer> requests;
			CoinsEmissionRequestFrame::loadCoinsEmissionRequests(account.getPublicKey(),
                requests, app.getDatabase());
			REQUIRE(requests.size() == 0);
		}
		SECTION("Can not create for the same reference")
		{
            applyCoinsEmissionRequest(app, root, rootSeq++,
            account.getPublicKey(), amount, 0, asset, "ref",
            MANAGE_COINS_EMISSION_REQUEST_CREATE,
            MANAGE_COINS_EMISSION_REQUEST_REFERENCE_DUPLICATION   );
		}
	}
	SECTION("Can create request to be fulfilled at once if emitted")
	{

        auto masterBalanceFrameBeforePreEmission = BalanceFrame::loadBalance(app.getMasterID(),
            app.getBaseAsset(),
            app.getDatabase(), nullptr);

		auto issuanceKey = getIssuanceKey();
		std::vector<PreEmission> preEmissions = { createPreEmission(issuanceKey, app.getConfig().EMISSION_UNIT, SecretKey::random().getStrKeyPublic()),
			createPreEmission(issuanceKey, app.getConfig().EMISSION_UNIT, SecretKey::random().getStrKeyPublic()) };
        applyUploadPreemissions(app, root, rootSeq++, preEmissions);
        auto masterBalanceFrameAfterPreEmission = BalanceFrame::loadBalance(app.getMasterID(),
            app.getBaseAsset(),
            app.getDatabase(), nullptr);
        REQUIRE(masterBalanceFrameBeforePreEmission->getAmount() ==
            masterBalanceFrameAfterPreEmission->getAmount() - 2 * app.getConfig().EMISSION_UNIT);


		applyCoinsEmissionRequest(app, root, rootSeq++,
            account.getPublicKey(), 2*amount, 0, asset, "ref",
            MANAGE_COINS_EMISSION_REQUEST_CREATE, MANAGE_COINS_EMISSION_REQUEST_SUCCESS, true);
        auto targetBalance = loadBalance(root.getPublicKey(), app, true);
        REQUIRE(targetBalance->getAmount() % app.getConfig().EMISSION_UNIT == 0);

        auto masterBalanceFrameAfterEmission = BalanceFrame::loadBalance(app.getMasterID(),
            app.getBaseAsset(),
            app.getDatabase(), nullptr);
        REQUIRE(masterBalanceFrameBeforePreEmission->getAmount() ==
            masterBalanceFrameAfterEmission->getAmount());
	}

	SECTION("Request does not exists")
	{
    
		applyCoinsEmissionRequest(app, root, rootSeq++, account.getPublicKey(),
            0, 123, asset, "", MANAGE_COINS_EMISSION_REQUEST_DELETE,
            MANAGE_COINS_EMISSION_REQUEST_NOT_FOUND);
	}
}
