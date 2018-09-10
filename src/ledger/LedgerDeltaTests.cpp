// Copyright 2015 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "util/Timer.h"
#include "main/test.h"
#include "ledger/LedgerDeltaImpl.h"
#include "main/Application.h"
#include "LedgerTestUtils.h"
#include "ledger/LedgerManager.h"
#include "ledger/AccountFrame.h"
#include "test/test_marshaler.h"

using namespace stellar;

TEST_CASE("Ledger delta", "[ledger][ledgerdelta]")
{
    Config cfg(getTestConfig());
    VirtualClock clock;
    Application::pointer app = Application::create(clock, cfg);
    app->start();
    LedgerHeader& curHeader = app->getLedgerManager().getCurrentLedgerHeader();
    LedgerHeader orgHeader = curHeader;

    LedgerDeltaImpl deltaImpl(curHeader, app->getDatabase());
    LedgerDelta& delta = deltaImpl;

    SECTION("header changes")
    {
        SECTION("commit top")
        {
            LedgerHeader expHeader = curHeader;
            for (auto& generator : expHeader.idGenerators)
            {
                if (generator.entryType == LedgerEntryType::BALANCE)
                    generator.idPool++;
            }

            SECTION("top")
            {
                delta.getHeaderFrame().generateID(LedgerEntryType::BALANCE);
                delta.commit();
            }
            SECTION("nested")
            {
                LedgerDeltaImpl delta2Impl(delta);
                LedgerDelta& delta2 = delta2Impl;
                delta2.getHeaderFrame().generateID(LedgerEntryType::BALANCE);
                SECTION("inner no op")
                {
                    LedgerDeltaImpl delta3(delta2);
                }
                SECTION("inner rollback")
                {
                    LedgerDeltaImpl delta3Impl(delta2);
                    LedgerDelta& delta3 = delta3Impl;
                    delta3.getHeaderFrame().generateID(LedgerEntryType::BALANCE);
                    delta3.rollback();
                }
                delta2.commit();
                delta.commit();
            }
            SECTION("nested2")
            {
                LedgerDeltaImpl delta2(delta);
                {
                    LedgerDeltaImpl delta3Impl(delta2);
                    LedgerDelta& delta3 = delta3Impl;
                    delta3.getHeaderFrame().generateID(LedgerEntryType::BALANCE);
                    delta3.commit();
                }
                static_cast<LedgerDelta&>(delta2).commit();
                delta.commit();
            }
            REQUIRE(curHeader == expHeader);
        }
        SECTION("rollback")
        {
            delta.getHeader().idGenerators[0].idPool++;
            delta.rollback();
            REQUIRE(curHeader == orgHeader);
        }
    }

    SECTION("delta object operations")
    {
        size_t const nbAccounts = 36;
        size_t const nbAccountsGroupSize = 9;

        using MapAccounts =
            std::map<LedgerKey, AccountFrame::pointer, LedgerEntryIdCmp>;

        MapAccounts orgAccounts;
        std::vector<AccountFrame::pointer> accounts;
        {
            uint32 s = delta.getHeader().ledgerSeq;
            auto aEntries =
                LedgerTestUtils::generateValidAccountEntries(nbAccounts);
            accounts.reserve(nbAccounts);
            for (auto a : aEntries)
            {
                LedgerEntry le;
                le.data.type(LedgerEntryType::ACCOUNT);
                le.data.account() = a;
                le.lastModifiedLedgerSeq = s;
                auto newA = std::make_shared<AccountFrame>(le);
                accounts.emplace_back(newA);
                orgAccounts.emplace(std::make_pair(newA->getKey(), newA));
            }
        }

        MapAccounts accountsByKey;

        auto addEntries =
            [&](size_t start, size_t end, LedgerDelta& d, MapAccounts& aKeys)
        {
            for (size_t i = start; i < end; i++)
            {
                auto a = accounts.at(i);
                a->mEntry.lastModifiedLedgerSeq = d.getHeader().ledgerSeq;
                d.addEntry(*a);
                aKeys.insert(std::make_pair(a->getKey(), a));
            }
        };

        auto modEntries =
            [&](size_t start, size_t end, LedgerDelta& d, MapAccounts& aKeys)
        {
            for (size_t i = start; i < end; i++)
            {
                auto a = accounts.at(i);
                auto key = a->getKey();
                auto it = aKeys.find(key);
                if (it != aKeys.end())
                {
                    a = it->second;
                }
                d.recordEntry(*a);
                // TODO check after comment
                auto newA = std::make_shared<AccountFrame>(a->mEntry);
                newA->mEntry.lastModifiedLedgerSeq = d.getHeader().ledgerSeq;
                d.modEntry(*newA);
                aKeys[key] = newA;
            }
        };

        auto delEntries =
            [&](size_t start, size_t end, LedgerDelta& d, MapAccounts& aKeys)
        {
            for (size_t i = start; i < end; i++)
            {
                auto a = accounts.at(i);
                auto key = a->getKey();
                auto it = aKeys.find(key);
                if (it != aKeys.end())
                {
                    a = it->second;
                }
                d.recordEntry(*a);
                d.deleteEntry(key);
                aKeys[key] = nullptr;
            }
        };

        delta.getHeader().ledgerSeq++;

        // builds a delta containing
        // [adds N][mods N][dels N]

        // add entries to the top level delta
        addEntries(0, nbAccountsGroupSize, delta, accountsByKey);
        // modify entries
        modEntries(nbAccountsGroupSize, nbAccountsGroupSize * 2, delta,
                   accountsByKey);
        // delete entries
        delEntries(nbAccountsGroupSize * 2, nbAccountsGroupSize * 3, delta,
                   accountsByKey);

        auto checkChanges =
            [&](LedgerDelta& d, size_t nbAdds, size_t nbMods, size_t nbDels,
                size_t nbStates, MapAccounts const& orgData)
        {
            auto changes = d.getChanges();
            size_t expectedChanges = nbAdds + nbMods + nbDels + nbStates;
            size_t adds = 0, mods = 0, dels = 0, states = 0;

            bool gotState = false;
            LedgerKey stateKey;
            for (auto const& c : changes)
            {
                switch (c.type())
                {
                case LedgerEntryChangeType::CREATED:
                {
                    REQUIRE(!gotState);
                    auto const& createdEntry = c.created();
                    auto key = LedgerEntryKey(createdEntry);
                    REQUIRE(createdEntry == accountsByKey.at(key)->mEntry);
                    adds++;
                }
                break;
                case LedgerEntryChangeType::REMOVED:
                {
                    auto const& removedEntry = c.removed();
                    if (gotState)
                    {
                        REQUIRE(stateKey == removedEntry);
                        gotState = false;
                    }
                    REQUIRE(accountsByKey[removedEntry] == nullptr);
                    dels++;
                }
                break;
                case LedgerEntryChangeType::UPDATED:
                {
                    auto const& updatedEntry = c.updated();
                    auto key = LedgerEntryKey(updatedEntry);
                    if (gotState)
                    {
                        REQUIRE(key == stateKey);
                        gotState = false;
                    }
                    REQUIRE(updatedEntry == accountsByKey.at(key)->mEntry);
                    mods++;
                }
                break;
                case LedgerEntryChangeType::STATE:
                {
                    REQUIRE(!gotState);
                    gotState = true;
                    auto const& state = c.state();
                    auto key = LedgerEntryKey(state);
                    stateKey = key;
                    REQUIRE(state == orgData.at(key)->mEntry);
                    states++;
                }
                break;
                }
            }
            REQUIRE(changes.size() == expectedChanges);
            REQUIRE(!gotState);
            REQUIRE(adds == nbAdds);
            REQUIRE(mods == nbMods);
            REQUIRE(dels == nbDels);
            REQUIRE(states == nbStates);
        };

        checkChanges(delta, nbAccountsGroupSize, nbAccountsGroupSize,
                     nbAccountsGroupSize, nbAccountsGroupSize * 2, orgAccounts);

        MapAccounts orgAccountsBeforeD2 = accountsByKey;
        orgAccountsBeforeD2.insert(orgAccounts.begin(), orgAccounts.end());

        SECTION("add more entries")
        {
            SECTION("commit")
            {
                LedgerDeltaImpl delta2(delta);
                addEntries(nbAccountsGroupSize * 3, nbAccountsGroupSize * 4,
                           delta2, accountsByKey);
                static_cast<LedgerDelta&>(delta2).commit();
                checkChanges(delta, nbAccountsGroupSize * 2,
                             nbAccountsGroupSize, nbAccountsGroupSize,
                             nbAccountsGroupSize * 2, orgAccounts);
            }
            SECTION("rollback")
            {
                LedgerDeltaImpl delta2(delta);
                MapAccounts accountsByKey2;
                addEntries(nbAccountsGroupSize * 3, nbAccountsGroupSize * 4,
                           delta2, accountsByKey2);
                static_cast<LedgerDelta&>(delta2).rollback();
                checkChanges(delta, nbAccountsGroupSize, nbAccountsGroupSize,
                             nbAccountsGroupSize, nbAccountsGroupSize * 2,
                             orgAccounts);
            }
        }
        SECTION("modified entries")
        {
            LedgerDeltaImpl delta2(delta);
            MapAccounts modAccounts = accountsByKey;

            // modify entries that were added and modified
            size_t start = nbAccountsGroupSize * 2 / 3;
            modEntries(start, start + nbAccountsGroupSize, delta2, modAccounts);
            // add modified entries that were not tracked so far
            modEntries(nbAccountsGroupSize * 3, nbAccountsGroupSize * 4, delta2,
                       modAccounts);

            SECTION("commit")
            {
                accountsByKey = modAccounts;
                checkChanges(delta2, 0, nbAccountsGroupSize * 2, 0,
                             nbAccountsGroupSize, orgAccountsBeforeD2);
                static_cast<LedgerDelta&>(delta2).commit();
                checkChanges(delta, nbAccountsGroupSize,
                             nbAccountsGroupSize * 2, nbAccountsGroupSize,
                             nbAccountsGroupSize * 3, orgAccounts);
            }
            SECTION("rollback")
            {
                static_cast<LedgerDelta&>(delta2).rollback();
                checkChanges(delta, nbAccountsGroupSize, nbAccountsGroupSize,
                             nbAccountsGroupSize, nbAccountsGroupSize * 2,
                             orgAccounts);
            }
        }
        SECTION("deleted entries")
        {
            LedgerDeltaImpl delta2Impl(delta);
            LedgerDelta& delta2 = delta2Impl;
            MapAccounts delAccounts = accountsByKey;

            // delete entries that were added and modified
            size_t start = nbAccountsGroupSize * 2 / 3;
            delEntries(start, start + nbAccountsGroupSize, delta2, delAccounts);
            // add deleted entries that were not tracked so far
            delEntries(nbAccountsGroupSize * 3, nbAccountsGroupSize * 4, delta2,
                       delAccounts);

            SECTION("commit")
            {
                accountsByKey = delAccounts;
                checkChanges(delta2, 0, 0, nbAccountsGroupSize * 2,
                             nbAccountsGroupSize, orgAccountsBeforeD2);
                delta2.commit();
                // adds/mods were replaced by a delete
                // adds+del result in no-op
                size_t adds2del = nbAccountsGroupSize / 3;
                checkChanges(delta, nbAccountsGroupSize - adds2del,
                             nbAccountsGroupSize - start,
                             nbAccountsGroupSize * 3 - adds2del,
                             nbAccountsGroupSize * 3, orgAccounts);
            }
            SECTION("rollback")
            {
                delta2.rollback();
                checkChanges(delta, nbAccountsGroupSize, nbAccountsGroupSize,
                             nbAccountsGroupSize, nbAccountsGroupSize * 2,
                             orgAccounts);
            }
        }
    }
}
