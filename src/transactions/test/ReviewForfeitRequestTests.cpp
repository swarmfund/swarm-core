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

TEST_CASE("Review forfeit request", "[dep_tx][review_forfeit_request]")
{
    Config const& cfg = getTestConfig(0, Config::TESTDB_POSTGRESQL);

    VirtualClock clock;
    Application::pointer appPtr = Application::create(clock, cfg);
    Application& app = *appPtr;
    app.start();
    closeLedgerOn(app, 2, 1, 7, 2014);

	int64 amount = 10000;

    // set up world
    SecretKey root = getRoot();
	PublicKey rootPK = root.getPublicKey();
    SecretKey issuance = getIssuanceKey();
    SecretKey accountWithMoney = SecretKey::random();
	Salt rootSeq = 1;
    BalanceID balanceID = SecretKey::random().getPublicKey();

    // fund root account
    auto emissionAmount = 10 * app.getConfig().EMISSION_UNIT;
    auto asset = app.getBaseAsset();
    applyCreateAccountTx(app, root, accountWithMoney, rootSeq++, AccountType::GENERAL);

    fundAccount(app, root, issuance, rootSeq,
        accountWithMoney.getPublicKey(), emissionAmount);

    SECTION("Can create request")
	{
        auto result = applyManageForfeitRequestTx(app, accountWithMoney, accountWithMoney.getPublicKey(), rootSeq++, rootPK, 1);
		SECTION("Can accept")
		{
			applyReviewPaymentRequestTx(app, root, rootSeq++, result.success().paymentID);
		}
	}
}
