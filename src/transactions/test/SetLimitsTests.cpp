// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "main/Application.h"
#include "main/Config.h"
#include "util/Timer.h"
#include "overlay/LoopbackPeer.h"
#include "util/make_unique.h"
#include "main/test.h"
#include "TxTests.h"
#include "ledger/LedgerDelta.h"
#include "ledger/AccountLimitsFrame.h"
#include "ledger/AccountLimitsHelper.h"
#include "ledger/AccountTypeLimitsHelper.h"
#include "ledger/StatisticsHelper.h"
#include "test/test_marshaler.h"


using namespace stellar;
using namespace stellar::txtest;

typedef std::unique_ptr<Application> appPtr;

// Try setting each option to make sure it works
// try setting all at once
// try setting high threshold ones without the correct sigs
// make sure it doesn't allow us to add signers when we don't have the
// minbalance
TEST_CASE("set limits", "[dep_tx][set_limits]")
{
    using xdr::operator==;

    Config const& cfg = getTestConfig(0, Config::TESTDB_POSTGRESQL);

    VirtualClock clock;
    Application::pointer appPtr = Application::create(clock, cfg);
    Application& app = *appPtr;
    app.start();
	LedgerDelta delta(app.getLedgerManager().getCurrentLedgerHeader(),
		app.getDatabase());

	upgradeToCurrentLedgerVersion(app);

    // set up world
    SecretKey root = getRoot();
    AccountID rootPK = root.getPublicKey();
    SecretKey a1 = getAccount("A");

    Salt rootSeq = 1;

    applyCreateAccountTx(app, root, a1, rootSeq++, AccountType::GENERAL);

    Salt a1seq = 1;


    Limits limits;
    AccountID account = a1.getPublicKey();
    AccountType accountType = AccountType::GENERAL;
    limits.dailyOut = 100;
    limits.weeklyOut = 200;
    limits.monthlyOut = 300;
    limits.annualOut = 300;

	auto accountLimitsHelper = AccountLimitsHelper::Instance();
	auto accountTypeLimitsHelper = AccountTypeLimitsHelper::Instance();
	auto statisticsHelper = StatisticsHelper::Instance();

    SECTION("malformed")
    {
        applySetLimits(app, root, rootSeq++, nullptr, nullptr, limits, SetLimitsResultCode::MALFORMED);
        applySetLimits(app, root, rootSeq++, &account, &accountType, limits, SetLimitsResultCode::MALFORMED);
        limits.annualOut = 0;
        applySetLimits(app, root, rootSeq++, &account, nullptr, limits, SetLimitsResultCode::MALFORMED);
    }
    SECTION("success account limits setting")
    {
        applySetLimits(app, root, rootSeq++, &account, nullptr, limits);
        auto limitsAfter = accountLimitsHelper->loadLimits(account,
            app.getDatabase());
        REQUIRE(limitsAfter);
        REQUIRE(limitsAfter->getLimits() == limits);
        
        SECTION("success update if already set")
        {
            limits.annualOut = INT64_MAX;
            applySetLimits(app, root, rootSeq++, &account, nullptr, limits);
            auto limitsAfter = accountLimitsHelper->loadLimits(account,
                app.getDatabase());
            REQUIRE(limitsAfter);
            REQUIRE(limitsAfter->getLimits() == limits);
        }
        
    }

    SECTION("success account type default limits update")
    {
        auto limitsBefore = accountTypeLimitsHelper->loadLimits(accountType, app.getDatabase(), &delta);
        REQUIRE(!limitsBefore);

        applySetLimits(app, root, rootSeq++, nullptr, &accountType, limits);
        auto limitsAfterFrame = accountTypeLimitsHelper->loadLimits(accountType, app.getDatabase(), &delta);
        REQUIRE(limitsAfterFrame);
        auto limitsAfter = limitsAfterFrame->getLimits();
        REQUIRE(limitsAfter == limits);
        SECTION("it works for created accounts")
        {
            auto a2 = SecretKey::random();
			auto receiver = SecretKey::random();
            applyCreateAccountTx(app, root, a2, rootSeq++, accountType);
			applyCreateAccountTx(app, root, receiver, rootSeq++, accountType);
        }
    }



}

