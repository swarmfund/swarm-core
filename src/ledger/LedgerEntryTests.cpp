// Copyright 2015 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "main/Application.h"
#include "ledger/LedgerManager.h"
#include "util/Timer.h"
#include "main/test.h"
#include "lib/catch.hpp"
#include "util/Logging.h"
#include "AccountFrame.h"
#include "CoinsEmissionRequestFrame.h"
#include "LedgerDelta.h"
#include "xdrpp/marshal.h"
#include "xdrpp/autocheck.h"
#include "crypto/SecretKey.h"
#include "ledger/LedgerTestUtils.h"
#include "database/Database.h"
#include <utility>
#include <memory>
#include <unordered_map>

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

    SECTION("round trip with database")
    {
        std::vector<LedgerEntry> accounts(100);

        std::unordered_map<AccountID, LedgerEntry> accountsMap;

        for (auto& l : accounts)
        {
            l.data.type(ACCOUNT);
            auto& a = l.data.account();
            a = LedgerTestUtils::generateValidAccountEntry(5);
            accountsMap.insert(std::make_pair(a.accountID, l));
        }

        LedgerHeader lh;
        LedgerDelta delta(lh, db, false);

        // adding accounts
        for (auto const& l : accountsMap)
        {
            AccountFrame::pointer af = std::make_shared<AccountFrame>(l.second);
            af->storeAdd(delta, db);
            auto fromDb = AccountFrame::loadAccount(af->getID(), db);
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
            af->storeChange(delta, db);
            auto fromDb = AccountFrame::loadAccount(af->getID(), db);
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
				af->storeChange(delta, db);
				auto fromDb = AccountFrame::loadAccount(af->getID(), db);
				REQUIRE(af->getAccount() == fromDb->getAccount());
			}
		};


		SECTION("CoinsEmissionRequest")
		{
			std::unordered_map<AccountID, std::vector<CoinsEmissionRequestFrame::pointer>>
				requestMap;
			std::unordered_set<uint64> requestIDs;

			auto requestsProcessor = [&](std::function<int(LedgerEntry&)> proc)
			{
				entriesProcessor(
					[&](LedgerEntry& account)
				{
					AccountEntry& newA = account.data.account();
					uint8_t nbRequests = intGen() % 64;
					for (uint8_t i = 0; i < nbRequests; i++)
					{
						LedgerEntry le;
						le.data.type(COINS_EMISSION_REQUEST);
						auto& request = le.data.coinsEmissionRequest();
						request = LedgerTestUtils::generateCoinsEmissionRequestEntry(5000);
						request.issuer = newA.accountID;
					}
				});
			};

			// create a bunch of coins emission requests
			requestsProcessor(
				[&](LedgerEntry& le)
			{
				auto& requests = requestMap[le.data.coinsEmissionRequest().issuer];

				LedgerKey thisKey = LedgerEntryKey(le);

				if (std::find(requestIDs.begin(), requestIDs.end(),
					le.data.coinsEmissionRequest().requestID) == requestIDs.end())
				{   
					auto req = std::make_shared<CoinsEmissionRequestFrame>(le);
					req->storeAdd(delta, db);
					requests.emplace_back(req);
					requestIDs.insert(req->getCoinsEmissionRequest().requestID);
					return 1;
				}
				return 0;
			});

			app->getLedgerManager().checkDbState();

			// modify requests
			requestsProcessor([&](LedgerEntry& le)
			{
				auto& requests = requestMap[le.data.coinsEmissionRequest().issuer];
				if (requests.size() != 0)
				{
					size_t indexToChange = intGen() % requests.size();
					auto r = requests[indexToChange];
					// change all but issuer and requestID and emissionID

					auto& newR = le.data.coinsEmissionRequest();

					auto& thisR = r->getCoinsEmissionRequest();

					newR.requestID = thisR.requestID;
					newR.issuer = thisR.issuer;
					newR.requestID = thisR.requestID;

					thisR = newR;

					r->storeChange(delta, db);
					auto fromDb = CoinsEmissionRequestFrame::loadCoinsEmissionRequest(thisR.requestID, db);
					REQUIRE(thisR == fromDb->getCoinsEmissionRequest());
				}
				return 0;
			});

			app->getLedgerManager().checkDbState();

			// delete requests
			for (auto& reql : requestMap)
			{
				LedgerEntry& ale = accountsMap[reql.first];
				for (auto req : reql.second)
				{
					req->storeDelete(delta, db);
				}
				AccountFrame::pointer af = std::make_shared<AccountFrame>(ale);
				af->storeChange(delta, db);
				auto fromDb = AccountFrame::loadAccount(af->getID(), db);
				REQUIRE(af->getAccount() == fromDb->getAccount());
			}

			app->getLedgerManager().checkDbState();
		}


        // deleting accounts
        for (auto const& l : accountsMap)
        {
            AccountFrame::pointer af = std::make_shared<AccountFrame>(l.second);
            REQUIRE(AccountFrame::loadAccount(af->getID(), db) != nullptr);
            REQUIRE(AccountFrame::exists(db, af->getKey()));
            af->storeDelete(delta, db);
            REQUIRE(AccountFrame::loadAccount(af->getID(), db) == nullptr);
            REQUIRE(!AccountFrame::exists(db, af->getKey()));
        }

        app->getLedgerManager().checkDbState();
    }
}
}
