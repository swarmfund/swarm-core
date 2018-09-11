//
// Created by artem on 13.06.18.
//

#include <catch.hpp>
#include <src/main/Config.h>
#include <src/main/test.h>
#include <util/Timer.h>
#include <main/Application.h>
#include "ledger/LedgerDeltaImpl.h"
#include "ledger/LedgerManager.h"
#include "ledger/StatisticsFrame.h"
#include "ledger/EntryHelperLegacy.h"
#include <src/transactions/test/TxTests.h>
#include <transactions/test/test_helper/TestManager.h>

using namespace stellar;
using namespace stellar::txtest;

void validateStatsV2(StatisticsV2Frame& statsFrame, uint64_t amount)
{
    REQUIRE(statsFrame.getAnnualOutcome() == amount);
    REQUIRE(statsFrame.getMonthlyOutcome() == amount);
    REQUIRE(statsFrame.getWeeklyOutcome() == amount);
    REQUIRE(statsFrame.getDailyOutcome() == amount);
}

TEST_CASE("StatisticsV2 tests", "[tx][stats_v2]")
{
    Config cfg(getTestConfig(0, Config::TESTDB_POSTGRESQL));
    VirtualClock clock;
    Application::pointer app = Application::create(clock, cfg);
    app->start();
    TestManager::upgradeToCurrentLedgerVersion(*app);

    Database& db = app->getDatabase();
    LedgerManager& ledgerManager(app->getLedgerManager());
    LedgerDeltaImpl delta(ledgerManager.getCurrentLedgerHeader(), db);

    LedgerEntry ledgerEntry;
    ledgerEntry.data.type(LedgerEntryType::STATISTICS_V2);
    StatisticsV2Frame statisticsV2Frame(ledgerEntry);

    time_t startingPoint = txtest::getTestDate(1, 1, 2017);
    uint64_t amount = UINT64_MAX/2;
    statisticsV2Frame.add(amount, startingPoint);

    EntryHelperProvider::storeAddEntry(delta, db, statisticsV2Frame.mEntry);
    static_cast<LedgerDelta&>(delta).commit();
    uint32 ledgerSeq = 3;
    txtest::closeLedgerOn(*app, ledgerSeq++, startingPoint);

    SECTION("success")
    {
        SECTION("successful update")
        {
            REQUIRE(statisticsV2Frame.add(ONE, startingPoint));
            validateStatsV2(statisticsV2Frame, amount + ONE);
        }

        SECTION("successful immediate revert")
        {
            statisticsV2Frame.revert(ONE, startingPoint, startingPoint);
            validateStatsV2(statisticsV2Frame, amount - ONE);
        }

        SECTION("successful revert after year passed")
        {
            time_t currentTime = txtest::getTestDate(1, 1, 2018);
            statisticsV2Frame.revert(ONE, currentTime, startingPoint);
            validateStatsV2(statisticsV2Frame, 0);
        }

        SECTION("successful revert after month passed")
        {
            time_t currentTime = txtest::getTestDate(1, 2, 2017);
            statisticsV2Frame.revert(ONE, currentTime, startingPoint);
            REQUIRE(statisticsV2Frame.getAnnualOutcome() == amount - ONE);
            REQUIRE(statisticsV2Frame.getMonthlyOutcome() == 0);
            REQUIRE(statisticsV2Frame.getWeeklyOutcome() == 0);
            REQUIRE(statisticsV2Frame.getDailyOutcome() == 0);
        }

        SECTION("successful revert after week passed")
        {
            time_t currentTime = txtest::getTestDate(8, 1, 2017);
            statisticsV2Frame.revert(ONE, currentTime, startingPoint);
            REQUIRE(statisticsV2Frame.getAnnualOutcome() == amount - ONE);
            REQUIRE(statisticsV2Frame.getMonthlyOutcome() == amount - ONE);
            REQUIRE(statisticsV2Frame.getWeeklyOutcome() == 0);
            REQUIRE(statisticsV2Frame.getDailyOutcome() == 0);
        }

        SECTION("successful revert after one day passed")
        {
            time_t currentTime = txtest::getTestDate(2, 1, 2017);
            statisticsV2Frame.revert(ONE, currentTime, startingPoint);
            REQUIRE(statisticsV2Frame.getAnnualOutcome() == amount - ONE);
            REQUIRE(statisticsV2Frame.getMonthlyOutcome() == amount - ONE);
            REQUIRE(statisticsV2Frame.getWeeklyOutcome() == amount - ONE);
            REQUIRE(statisticsV2Frame.getDailyOutcome() == 0);
        }
    }

    SECTION("overflow stats")
    {
        uint64_t bigAmount = UINT64_MAX - 1;
        REQUIRE(!statisticsV2Frame.add(bigAmount, startingPoint));
    }

    SECTION("revert too much")
    {
        uint64_t bigAmount = amount + 1;
        SECTION("successful revert after year passed")
        {
            time_t currentTime = txtest::getTestDate(1, 1, 2018);
            statisticsV2Frame.revert(bigAmount, currentTime, startingPoint);
        }

        SECTION("invalid annual outcome")
        {
            time_t currentTime = txtest::getTestDate(1, 2, 2017);
            REQUIRE_THROWS(statisticsV2Frame.revert(bigAmount, currentTime, startingPoint));
        }

        SECTION("invalid monthly outcome")
        {
            //clear monthly outcome by updating it in next month
            time_t currentTime = txtest::getTestDate(1, 2, 2017);
            statisticsV2Frame.add(amount, currentTime);
            //try to revert
            REQUIRE_THROWS(statisticsV2Frame.revert(bigAmount, currentTime, currentTime));
        }
        SECTION("invalid weekly outcome")
        {
            //clear weekly outcome by updating it in the next week
            time_t currentTime = txtest::getTestDate(8, 1, 2017);
            statisticsV2Frame.add(amount, currentTime);
            //try to revert
            REQUIRE_THROWS(statisticsV2Frame.revert(bigAmount, currentTime, currentTime));
        }

        SECTION("invalid daily outcome")
        {
            //clear daily outcome by updating it in the next day
            time_t currentTime = txtest::getTestDate(2, 1, 2017);
            statisticsV2Frame.add(amount, currentTime);
            //try to revert
            REQUIRE_THROWS(statisticsV2Frame.revert(bigAmount, currentTime, currentTime));
        }

    }
}