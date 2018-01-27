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
#include "ledger/AccountLimitsFrame.h"
#include "ledger/AccountLimitsHelper.h"
#include "ledger/AccountTypeLimitsHelper.h"
#include "ledger/StatisticsHelper.h"
#include "test/test_marshaler.h"
#include "test_helper/CreateAccountTestHelper.h"
#include "test_helper/SetLimitsTestHelper.h"


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

    auto testManager = TestManager::make(app);

	LedgerDelta delta(app.getLedgerManager().getCurrentLedgerHeader(),
		app.getDatabase());

	upgradeToCurrentLedgerVersion(app);

    // set up world
    auto root = Account{ getRoot(), Salt(0) };
    auto a1 = Account { getAccount("A"), Salt(0) };

    CreateAccountTestHelper createAccountTestHelper(testManager);
    createAccountTestHelper.applyCreateAccountTx(root, a1.key.getPublicKey(), AccountType::GENERAL);

    Limits limits;
    AccountID account = a1.key.getPublicKey();
    AccountType accountType = AccountType::GENERAL;
    limits.dailyOut = 100;
    limits.weeklyOut = 200;
    limits.monthlyOut = 300;
    limits.annualOut = 300;

	auto accountLimitsHelper = AccountLimitsHelper::Instance();
	auto accountTypeLimitsHelper = AccountTypeLimitsHelper::Instance();
	auto statisticsHelper = StatisticsHelper::Instance();

    SetLimitsTestHelper setLimitsTestHelper(testManager);

    SECTION("malformed")
    {
        setLimitsTestHelper.applySetLimitsTx(root, nullptr, nullptr, limits, SetLimitsResultCode::MALFORMED);
        setLimitsTestHelper.applySetLimitsTx(root, &account, &accountType, limits, SetLimitsResultCode::MALFORMED);
        limits.annualOut = 0;
        setLimitsTestHelper.applySetLimitsTx(root, &account, nullptr, limits, SetLimitsResultCode::MALFORMED);
    }
    SECTION("success account limits setting")
    {
        setLimitsTestHelper.applySetLimitsTx(root, &account, nullptr, limits);
        auto limitsAfter = accountLimitsHelper->loadLimits(account,
            app.getDatabase());
        REQUIRE(limitsAfter);
        REQUIRE(limitsAfter->getLimits() == limits);
        
        SECTION("success update if already set")
        {
            limits.annualOut = INT64_MAX;
            setLimitsTestHelper.applySetLimitsTx(root, &account, nullptr, limits);
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

        setLimitsTestHelper.applySetLimitsTx(root, nullptr, &accountType, limits);
        auto limitsAfterFrame = accountTypeLimitsHelper->loadLimits(accountType, app.getDatabase(), &delta);
        REQUIRE(limitsAfterFrame);
        auto limitsAfter = limitsAfterFrame->getLimits();
        REQUIRE(limitsAfter == limits);
        SECTION("it works for created accounts")
        {
            auto a2 = Account { SecretKey::random(), Salt(0)};
            auto receiver = Account { SecretKey::random(), Salt(0)};
            createAccountTestHelper.applyCreateAccountTx(root, a2.key.getPublicKey(), accountType);
            createAccountTestHelper.applyCreateAccountTx(root, receiver.key.getPublicKey(), accountType);
        }
    }
}