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

using namespace stellar;
using namespace stellar::txtest;

typedef std::unique_ptr<Application> appPtr;

TEST_CASE("Upload preemissions", "[tx][upload_preemissions]")
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
	auto preEmission = createPreEmission(issuanceKey, app.getConfig().EMISSION_UNIT,
        SecretKey::random().getStrKeyPublic());
    preEmissions.push_back(preEmission);
    
    auto account = SecretKey::random();
    applyCreateAccountTx(app, root, account, rootSeq++, GENERAL);
	SECTION("Non root account can't upload")
	{
		auto accountSeq = 1;
		auto uploadPreemission = createUploadPreemissions(app.getNetworkID(), account, accountSeq++, preEmissions);
		applyCheck(uploadPreemission, delta, app);
		REQUIRE(getFirstResult(*uploadPreemission).code() ==
            OperationResultCode::opNOT_ALLOWED);
	}

    SECTION("Too much emissions")
    {
        for (int i = 0; i < app.getConfig().PREEMISSIONS_PER_OP + 1; i++)
        {
            auto preEmission2 = PreEmission();
            preEmission2.serialNumber = SecretKey::random().getStrKeyPublic();
            preEmission2.amount = 500;
            preEmission2.asset = app.getBaseAsset();
            auto data2 = preEmission2.serialNumber + ":" + std::to_string(preEmission2.amount)
                + app.getBaseAsset();
            DecoratedSignature extraDecoratedSig;
            extraDecoratedSig.signature = root.sign(data2);
            extraDecoratedSig.hint = PubKeyUtils::getHint(root.getPublicKey());
            preEmissions[0].signatures[0] = extraDecoratedSig;

            preEmission2.signatures.push_back(extraDecoratedSig);
            preEmissions.push_back(preEmission2);
        }
        applyUploadPreemissions(app, root, rootSeq++, preEmissions, UPLOAD_PREEMISSIONS_MALFORMED);
    }
    SECTION("Invalid pre-emission")
    {
        SECTION("Bad amount")
        {
            auto preEmission = createPreEmission(issuanceKey, 0,
                SecretKey::random().getStrKeyPublic());
            applyUploadPreemissions(app, root, rootSeq++, { preEmission }, UPLOAD_PREEMISSIONS_MALFORMED_PREEMISSIONS);
            preEmission = createPreEmission(issuanceKey, app.getConfig().EMISSION_UNIT + 1,
                SecretKey::random().getStrKeyPublic());
            applyUploadPreemissions(app, root, rootSeq++, { preEmission }, UPLOAD_PREEMISSIONS_MALFORMED_PREEMISSIONS);
        }
        SECTION("Bad serialNumber")
        {
            auto preEmission = createPreEmission(issuanceKey, app.getConfig().EMISSION_UNIT, "");
            applyUploadPreemissions(app, root, rootSeq++, { preEmission }, UPLOAD_PREEMISSIONS_MALFORMED_PREEMISSIONS);
        }
        SECTION("Bad asset")
        {
            auto preEmission = createPreEmission(issuanceKey, app.getConfig().EMISSION_UNIT,
                SecretKey::random().getStrKeyPublic(), "");
            applyUploadPreemissions(app, root, rootSeq++, { preEmission }, UPLOAD_PREEMISSIONS_MALFORMED_PREEMISSIONS);
        }
        SECTION("No signatures")
        {
            preEmissions[0].signatures.resize(0);
            applyUploadPreemissions(app, root, rootSeq++, preEmissions, UPLOAD_PREEMISSIONS_MALFORMED_PREEMISSIONS);
        }
        SECTION("Wrong signature")
        {
            DecoratedSignature fakeDecoratedSig;
            fakeDecoratedSig.signature = root.sign("fake data");
            fakeDecoratedSig.hint = PubKeyUtils::getHint(root.getPublicKey());
            preEmissions[0].signatures[0] = fakeDecoratedSig;
            applyUploadPreemissions(app, root, rootSeq++, preEmissions, UPLOAD_PREEMISSIONS_MALFORMED_PREEMISSIONS);
        }
        SECTION("Signature by random account")
        {
            auto account = SecretKey::random();
            applyCreateAccountTx(app, root, account, rootSeq++, GENERAL);
            DecoratedSignature fakeDecoratedsig;
            auto data = preEmissions[0].serialNumber + ":" + std::to_string(preEmissions[0].amount);
            fakeDecoratedsig.signature = account.sign(data);
            fakeDecoratedsig.hint = PubKeyUtils::getHint(root.getPublicKey());
            preEmissions[0].signatures[0] = fakeDecoratedsig;
            applyUploadPreemissions(app, root, rootSeq++, preEmissions, UPLOAD_PREEMISSIONS_MALFORMED_PREEMISSIONS);
        }

        SECTION("Asset not found")
        {
            auto preEmission = createPreEmission(issuanceKey, app.getConfig().EMISSION_UNIT,
                SecretKey::random().getStrKeyPublic(), "ABTC");
            applyUploadPreemissions(app, root, rootSeq++, { preEmission }, UPLOAD_PREEMISSIONS_ASSET_NOT_FOUND);
        }

    }
    SECTION("Emissions Uploaded")
    {
        applyUploadPreemissions(app, root, rootSeq++, preEmissions);
        SECTION("Do not upload another with the same serial")
        {
            applyUploadPreemissions(app, root, rootSeq++, preEmissions, UPLOAD_PREEMISSIONS_SERIAL_DUPLICATION);
        }
    }

    SECTION("Lots of emissions uploaded and 1 used")
    {
        for (int i = 0; i < app.getConfig().PREEMISSIONS_PER_OP - 1; i++)
        {
            auto preEmission = createPreEmission(issuanceKey, app.getConfig().EMISSION_UNIT,
                SecretKey::random().getStrKeyPublic());
            preEmissions.push_back(preEmission);
        }
        applyUploadPreemissions(app, root, rootSeq++, preEmissions);
        auto requestEntry = makeCoinsEmissionRequest(root.getPublicKey(),
            account.getPublicKey(), 0, app.getConfig().EMISSION_UNIT);
		applyReviewCoinsEmissionRequest(app, root, rootSeq++, requestEntry, true, "");

        SECTION("Do not upload another with the same serial")
        {
            applyUploadPreemissions(app, root, rootSeq++, preEmissions, UPLOAD_PREEMISSIONS_SERIAL_DUPLICATION);
        }
    }

}
