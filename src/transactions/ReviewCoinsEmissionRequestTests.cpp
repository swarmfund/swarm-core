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
#include "ledger/LedgerDelta.h"
#include "crypto/SHA.h"
#include "crypto/Hex.h"
#include "ledger/CoinsEmissionFrame.h"

using namespace stellar;
using namespace stellar::txtest;

typedef std::unique_ptr<Application> appPtr;

TEST_CASE("Review coins emission request", "[tx][review_emission_request]")
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
	Salt rootSeq = 1;
    std::vector<PreEmission> emptyPreEmissions;
    std::vector<PreEmission> preEmissions;
    auto issuanceKey = getIssuanceKey();
    auto emittedAmount = 2 * app.getConfig().EMISSION_UNIT;
	auto preEmission1 = createPreEmission(issuanceKey, app.getConfig().EMISSION_UNIT,
        SecretKey::random().getStrKeyPublic());
    auto preEmission2 = createPreEmission(issuanceKey, app.getConfig().EMISSION_UNIT,
        SecretKey::random().getStrKeyPublic());
    preEmissions.push_back(preEmission1);
    preEmissions.push_back(preEmission2);

    auto account = SecretKey::random();
    applyCreateAccountTx(app, root, account, rootSeq++, GENERAL);
	SECTION("Non root account can't review")
	{
		auto requestEntry = makeCoinsEmissionRequest(root.getPublicKey(), account.getPublicKey(), 1, 1000);
		auto accountSeq = 1;
		auto reviewEmission = createReviewCoinsEmissionRequest(app.getNetworkID(), account, accountSeq++, requestEntry, true, "");
		applyCheck(reviewEmission, delta, app);
		REQUIRE(getFirstResult(*reviewEmission).code() == OperationResultCode::opNOT_ALLOWED);
	}
	SECTION("Malformed")
	{
		auto validRequestEntry = makeCoinsEmissionRequest(root.getPublicKey(), root.getPublicKey(), 123, emittedAmount);
		SECTION("Invalid amount")
		{
			auto requestEntry = validRequestEntry;
			requestEntry.amount = 0;
			applyReviewCoinsEmissionRequest(app, root, rootSeq++, requestEntry, true, "", REVIEW_COINS_EMISSION_REQUEST_MALFORMED);
		}
		SECTION("Invalid reason")
		{
			applyReviewCoinsEmissionRequest(app, root, rootSeq++, validRequestEntry, true, "invalid_reason", REVIEW_COINS_EMISSION_REQUEST_INVALID_REASON);
		}
		SECTION("Invalid asset")
		{
			auto requestEntry = validRequestEntry;
            requestEntry.asset = "";
			applyReviewCoinsEmissionRequest(app, root, rootSeq++, requestEntry, true, "", REVIEW_COINS_EMISSION_REQUEST_MALFORMED);
		}

	}
	SECTION("Does not exists")
	{
		auto requestEntry = makeCoinsEmissionRequest(root.getPublicKey(), root.getPublicKey(), 123, app.getConfig().EMISSION_UNIT);
		applyReviewCoinsEmissionRequest(app, root, rootSeq++, requestEntry, true, "", REVIEW_COINS_EMISSION_REQUEST_NOT_FOUND);
	}

	SECTION("Not enough preemissions")
	{
		auto requestEntry = makeCoinsEmissionRequest(root.getPublicKey(), account.getPublicKey(), 0, app.getConfig().EMISSION_UNIT);
		applyReviewCoinsEmissionRequest(app, root, rootSeq++, requestEntry,
            true, "", REVIEW_COINS_EMISSION_REQUEST_NOT_ENOUGH_PREEMISSIONS);
	}

	SECTION("Manual emission")
	{
        applyUploadPreemissions(app, root, rootSeq++, preEmissions);
		auto requestEntry = makeCoinsEmissionRequest(root.getPublicKey(), account.getPublicKey(), 0, emittedAmount);
		applyReviewCoinsEmissionRequest(app, root, rootSeq++, requestEntry, true, "");
        applyReviewCoinsEmissionRequest(app, root, rootSeq++, requestEntry, true, "", REVIEW_COINS_EMISSION_REQUEST_NOT_ENOUGH_PREEMISSIONS);
	}

	SECTION("Manual emission reference")
	{
		auto request = makeCoinsEmissionRequest(root.getPublicKey(), account.getPublicKey(), 0, app.getConfig().EMISSION_UNIT);
		request.reference = "";
		// empty reference works
		applyUploadPreemissions(app, root, rootSeq++, { createPreEmission(issuanceKey, app.getConfig().EMISSION_UNIT, SecretKey::random().getStrKeyPublic()) });
		applyReviewCoinsEmissionRequest(app, root, rootSeq++, request, true, "");
		applyUploadPreemissions(app, root, rootSeq++, { createPreEmission(issuanceKey, app.getConfig().EMISSION_UNIT, SecretKey::random().getStrKeyPublic()) });
		applyReviewCoinsEmissionRequest(app, root, rootSeq++, request, true, "");
		// non empty
		request.reference = "random_ref";
		applyUploadPreemissions(app, root, rootSeq++, { createPreEmission(issuanceKey, app.getConfig().EMISSION_UNIT, SecretKey::random().getStrKeyPublic()) });
		applyReviewCoinsEmissionRequest(app, root, rootSeq++, request, true, "");
		applyReviewCoinsEmissionRequest(app, root, rootSeq++, request, true, "", REVIEW_COINS_EMISSION_REQUEST_REFERENCE_DUPLICATION);
	}
	SECTION("Given valid emission request")
	{
		auto requestID = applyCoinsEmissionRequest(app, root, rootSeq++, account.getPublicKey(), app.getConfig().EMISSION_UNIT);
		auto request = loadCoinsEmissionRequest(requestID, app);
		SECTION("Not equal")
		{
			CoinsEmissionRequestEntry validRequestEntry = request->getCoinsEmissionRequest();
			SECTION("Different amount")
			{
				auto requestEntry = validRequestEntry;
				requestEntry.amount *= 2;
				applyReviewCoinsEmissionRequest(app, root, rootSeq++, requestEntry, true, "", REVIEW_COINS_EMISSION_REQUEST_NOT_EQUAL);
			}
		}
		SECTION("Approve")
		{
            applyUploadPreemissions(app, root, rootSeq++, preEmissions);
			applyReviewCoinsEmissionRequest(app, root, rootSeq++, request->getCoinsEmissionRequest(), true, "");
			applyReviewCoinsEmissionRequest(app, root, rootSeq++, request->getCoinsEmissionRequest(), true, "", REVIEW_COINS_EMISSION_REQUEST_ALREADY_REVIEWED);
			applyReviewCoinsEmissionRequest(app, root, rootSeq++, request->getCoinsEmissionRequest(), false, "",REVIEW_COINS_EMISSION_REQUEST_ALREADY_REVIEWED);
		}
		SECTION("Reject")
		{
			applyReviewCoinsEmissionRequest(app, root, rootSeq++, request->getCoinsEmissionRequest(), false, "not allowed");
		}
	}
}
