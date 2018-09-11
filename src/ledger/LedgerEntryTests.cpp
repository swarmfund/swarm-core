// Copyright 2015 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "main/Application.h"
#include "ledger/LedgerManager.h"
#include "util/Timer.h"
#include "main/test.h"
#include "AccountFrame.h"
#include "AccountHelper.h"
#include "LedgerDeltaImpl.h"
#include "xdrpp/autocheck.h"
#include "ledger/LedgerTestUtils.h"
#include "test/test_marshaler.h"

using namespace stellar;

namespace LedgerEntryTests
{

TEST_CASE("Ledger Entry tests", "[ledgerentry]")
{
    Config cfg(getTestConfig(0));

    VirtualClock clock;
    Application::pointer app = Application::create(clock, cfg);
    app->start();
    Database& db = app->getDatabase();

	auto accountHelper = AccountHelper::Instance();

    SECTION("round trip with database")
    {
        std::vector<LedgerEntry> accounts(100);

        std::unordered_map<AccountID, LedgerEntry> accountsMap;

        for (auto& l : accounts)
        {
            l.data.type(LedgerEntryType::ACCOUNT);
            auto& a = l.data.account();
            a = LedgerTestUtils::generateValidAccountEntry(5);
            accountsMap.insert(std::make_pair(a.accountID, l));
        }

        LedgerHeader lh;
        LedgerDeltaImpl delta(lh, db, false);

        // adding accounts
        for (auto const& l : accountsMap)
        {
            AccountFrame::pointer af = std::make_shared<AccountFrame>(l.second);
            EntryHelperProvider::storeAddEntry(delta, db, af->mEntry);
            auto fromDb = accountHelper->loadAccount(af->getID(), db);
            REQUIRE(af->getAccount() == fromDb->getAccount());
        }
        app->getLedgerManager().checkDbState();

        // updating accounts
        for (auto& l : accountsMap)
        {
            AccountEntry& newA = l.second.data.account();
            // replace by completely new object
            newA = LedgerTestUtils::generateValidAccountEntry(5);

            // preserve the accountID as it's the key
            newA.accountID = l.first;

            AccountFrame::pointer af = std::make_shared<AccountFrame>(l.second);
            EntryHelperProvider::storeChangeEntry(delta, db, af->mEntry);
            auto fromDb = accountHelper->loadAccount(af->getID(), db);
            REQUIRE(af->getAccount() == fromDb->getAccount());
        }
        app->getLedgerManager().checkDbState();

		autocheck::generator<uint8_t> intGen;

		auto entriesProcessor =
			[&](std::function<void(LedgerEntry&)> accountProc)
		{
			for (auto& l : accountsMap)
			{
				accountProc(l.second);

				AccountFrame::pointer af =
					std::make_shared<AccountFrame>(l.second);
				EntryHelperProvider::storeChangeEntry(delta, db, af->mEntry);
				auto fromDb = accountHelper->loadAccount(af->getID(), db);
				REQUIRE(af->getAccount() == fromDb->getAccount());
			}
		};


        // deleting accounts
        for (auto const& l : accountsMap)
        {
            AccountFrame::pointer af = std::make_shared<AccountFrame>(l.second);
            REQUIRE(accountHelper->loadAccount(af->getID(), db) != nullptr);
            REQUIRE(accountHelper->exists(db, af->getKey()));
            EntryHelperProvider::storeDeleteEntry(delta, db, af->getKey());
            REQUIRE(accountHelper->loadAccount(af->getID(), db) == nullptr);
            REQUIRE(!accountHelper->exists(db, af->getKey()));
        }

        app->getLedgerManager().checkDbState();
    }
}
}
