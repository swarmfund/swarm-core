// Copyright 2015 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "util/Timer.h"
#include "main/Application.h"
#include "main/test.h"
#include "database/Database.h"
#include "ledger/LedgerDeltaImpl.h"
#include "ledger/LedgerManager.h"
#include "ledger/EntryHelperLegacy.h"
#include "ledger/AccountFrame.h"
#include "ledger/AccountHelper.h"
#include <xdrpp/autocheck.h>
#include "LedgerTestUtils.h"
#include "test/test_marshaler.h"

using namespace stellar;

TEST_CASE("Ledger entry db lifecycle", "[ledger]")
{
    Config cfg(getTestConfig());
    VirtualClock clock;
    Application::pointer app = Application::create(clock, cfg);
    app->start();
    LedgerDeltaImpl delta(app->getLedgerManager().getCurrentLedgerHeader(),
                          app->getDatabase());
    auto& db = app->getDatabase();
    for (size_t i = 0; i < 1000; ++i)
    {
        auto le =
            EntryHelperProvider::fromXDREntry(LedgerTestUtils::generateValidLedgerEntry(3));
        CHECK(!EntryHelperProvider::existsEntry(db, le->getKey()));
		EntryHelperProvider::storeAddOrChangeEntry(delta, db, le->mEntry);
        CHECK(EntryHelperProvider::existsEntry(db, le->getKey()));
		CHECK(EntryHelperProvider::storeLoadEntry(le->getKey(), db));
		EntryHelperProvider::storeDeleteEntry(delta, db, le->getKey());
        CHECK(!EntryHelperProvider::existsEntry(db, le->getKey()));
		CHECK(!EntryHelperProvider::storeLoadEntry(le->getKey(), db));
    }
}

TEST_CASE("single ledger entry insert SQL", "[singlesql][entrysql]")
{
    Config::TestDbMode mode = Config::TESTDB_ON_DISK_SQLITE;
#ifdef USE_POSTGRES
    if (!force_sqlite)
        mode = Config::TESTDB_POSTGRESQL;
#endif

    VirtualClock clock;
    Application::pointer app =
        Application::create(clock, getTestConfig(0, mode));
    app->start();

    LedgerDeltaImpl delta(app->getLedgerManager().getCurrentLedgerHeader(),
                          app->getDatabase());
    auto& db = app->getDatabase();
    auto le = EntryHelperProvider::fromXDREntry(LedgerTestUtils::generateValidLedgerEntry(3));
    auto ctx = db.captureAndLogSQL("ledger-insert");
	EntryHelperProvider::storeAddOrChangeEntry(delta, db, le->mEntry);
}

TEST_CASE("DB cache interaction with transactions", "[ledger][dbcache]")
{
    Config::TestDbMode mode = Config::TESTDB_ON_DISK_SQLITE;
#ifdef USE_POSTGRES
    if (!force_sqlite)
        mode = Config::TESTDB_POSTGRESQL;
#endif

    VirtualClock clock;
    Application::pointer app =
        Application::create(clock, getTestConfig(0, mode));
    app->start();

    auto& db = app->getDatabase();
    auto& session = db.getSession();

    EntryFrame::pointer le;
    do
    {
        le = EntryHelperProvider::fromXDREntry(LedgerTestUtils::generateValidLedgerEntry(3));
    } while (le->mEntry.data.type() != LedgerEntryType::ACCOUNT);

    auto key = le->getKey();

    {
        LedgerDeltaImpl delta(app->getLedgerManager().getCurrentLedgerHeader(),
                              app->getDatabase());
        soci::transaction sqltx(session);
        EntryHelperProvider::storeAddOrChangeEntry(delta, db, le->mEntry);
        sqltx.commit();
    }

    // The write should have removed it from the cache.
	auto accountHelper = AccountHelper::Instance();
    REQUIRE(!accountHelper->cachedEntryExists(key, db));

    AccountType accountType0, accountType1;

    {
        soci::transaction sqltx(session);
        LedgerDeltaImpl delta(app->getLedgerManager().getCurrentLedgerHeader(),
                              app->getDatabase());

        auto acc = accountHelper->loadAccount(key.account().accountID, db);
        REQUIRE(accountHelper->cachedEntryExists(key, db));

        accountType0 = acc->getAccount().accountType;
        acc->getAccount().accountType = AccountType::GENERAL;
        accountType1 = acc->getAccount().accountType;

        EntryHelperProvider::storeChangeEntry(delta, db, acc->mEntry);
        // Write should flush cache, put balance1 in DB _pending commit_.
        REQUIRE(!accountHelper->cachedEntryExists(key, db));

        acc = accountHelper->loadAccount(key.account().accountID, db);
        // Read should have populated cache.
        REQUIRE(accountHelper->cachedEntryExists(key, db));

        // Read-back value should be balance1
        REQUIRE(acc->getAccount().accountType == accountType1);

        LOG(INFO) << "accountType0: " << static_cast<int32_t >(accountType0);
        LOG(INFO) << "accountType1: " << static_cast<int32_t >(accountType1);

        // Scope-end will rollback sqltx and delta
    }

    // Rollback should have evicted changed value from cache.
    CHECK(!accountHelper->cachedEntryExists(key, db));

    auto acc = accountHelper->loadAccount(key.account().accountID, db);
    // Read should populate cache
    CHECK(accountHelper->cachedEntryExists(key, db));
    LOG(INFO) << "cached accountType: " << static_cast<int32_t >(acc->getAccount().accountType);

    CHECK(accountType0 == acc->getAccount().accountType);
}
