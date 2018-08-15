#include <catch.hpp>
#include <src/main/Config.h>
#include <src/main/test.h>
#include <util/Timer.h>
#include <main/Application.h>
#include "ledger/LedgerDeltaImpl.h"
#include "ledger/LedgerManager.h"
#include "StatisticsFrame.h"
#include "EntryHelperLegacy.h"
#include <src/transactions/test/TxTests.h>

using namespace stellar;

void validateStats(StatisticsFrame& statsFrame, uint64_t amount)
{
    REQUIRE(statsFrame.getAnnualOutcome() == amount);
    REQUIRE(statsFrame.getMonthlyOutcome() == amount);
    REQUIRE(statsFrame.getWeeklyOutcome() == amount);
    REQUIRE(statsFrame.getDailyOutcome() == amount);
}

TEST_CASE("Statistics tests", "[dep_tx][stats]")
{
    Config cfg(getTestConfig(0, Config::TESTDB_POSTGRESQL));
    VirtualClock clock;
    Application::pointer app = Application::create(clock, cfg);
    app->start();

    Database& db = app->getDatabase();
    LedgerManager& ledgerManager(app->getLedgerManager());
    LedgerDeltaImpl deltaImpl(ledgerManager.getCurrentLedgerHeader(), db);
    LedgerDelta& delta = deltaImpl;

    LedgerEntry ledgerEntry;
    ledgerEntry.data.type(LedgerEntryType::STATISTICS);
    StatisticsFrame statisticsFrame(ledgerEntry);

    time_t startingPoint = txtest::getTestDate(1, 1, 2017);
    uint64_t amount = UINT64_MAX/2;
    statisticsFrame.add(amount, startingPoint);

    EntryHelperProvider::storeAddEntry(delta, db, statisticsFrame.mEntry);
    delta.commit();
    uint32 ledgerSeq = 2;
    txtest::closeLedgerOn(*app, ledgerSeq++, startingPoint);

    SECTION("success")
    {
        SECTION("successful update")
        {
            REQUIRE(statisticsFrame.add(ONE, startingPoint));
            validateStats(statisticsFrame, amount + ONE);
        }

        SECTION("successful immediate revert")
        {
            statisticsFrame.revert(ONE, startingPoint, startingPoint);
            validateStats(statisticsFrame, amount - ONE);
        }

        SECTION("successful revert after year passed")
        {
            time_t currentTime = txtest::getTestDate(1, 1, 2018);
            statisticsFrame.revert(ONE, currentTime, startingPoint);
            validateStats(statisticsFrame, 0);
        }

        SECTION("successful revert after month passed")
        {
            time_t currentTime = txtest::getTestDate(1, 2, 2017);
            statisticsFrame.revert(ONE, currentTime, startingPoint);
            REQUIRE(statisticsFrame.getAnnualOutcome() == amount - ONE);
            REQUIRE(statisticsFrame.getMonthlyOutcome() == 0);
            REQUIRE(statisticsFrame.getWeeklyOutcome() == 0);
            REQUIRE(statisticsFrame.getDailyOutcome() == 0);
        }

        SECTION("successful revert after week passed")
        {
            time_t currentTime = txtest::getTestDate(8, 1, 2017);
            statisticsFrame.revert(ONE, currentTime, startingPoint);
            REQUIRE(statisticsFrame.getAnnualOutcome() == amount - ONE);
            REQUIRE(statisticsFrame.getMonthlyOutcome() == amount - ONE);
            REQUIRE(statisticsFrame.getWeeklyOutcome() == 0);
            REQUIRE(statisticsFrame.getDailyOutcome() == 0);
        }

        SECTION("successful revert after one day passed")
        {
            time_t currentTime = txtest::getTestDate(2, 1, 2017);
            statisticsFrame.revert(ONE, currentTime, startingPoint);
            REQUIRE(statisticsFrame.getAnnualOutcome() == amount - ONE);
            REQUIRE(statisticsFrame.getMonthlyOutcome() == amount - ONE);
            REQUIRE(statisticsFrame.getWeeklyOutcome() == amount - ONE);
            REQUIRE(statisticsFrame.getDailyOutcome() == 0);
        }
    }

    SECTION("overflow stats")
    {
        uint64_t bigAmount = UINT64_MAX - 1;
        REQUIRE(!statisticsFrame.add(bigAmount, startingPoint));
    }

    SECTION("revert too much")
    {
        uint64_t bigAmount = amount + 1;
        SECTION("successful revert after year passed")
        {
            time_t currentTime = txtest::getTestDate(1, 1, 2018);
            statisticsFrame.revert(bigAmount, currentTime, startingPoint);
        }

        SECTION("invalid annual outcome")
        {
            time_t currentTime = txtest::getTestDate(1, 2, 2017);
            REQUIRE_THROWS(statisticsFrame.revert(bigAmount, currentTime, startingPoint));
        }

        SECTION("invalid monthly outcome")
        {
            //clear monthly outcome by updating it in next month
            time_t currentTime = txtest::getTestDate(1, 2, 2017);
            statisticsFrame.add(amount, currentTime);
            //try to revert
            REQUIRE_THROWS(statisticsFrame.revert(bigAmount, currentTime, currentTime));
        }

        SECTION("invalid weekly outcome")
        {
            //clear weekly outcome by updating it in the next week
            time_t currentTime = txtest::getTestDate(8, 1, 2017);
            statisticsFrame.add(amount, currentTime);
            //try to revert
            REQUIRE_THROWS(statisticsFrame.revert(bigAmount, currentTime, currentTime));
        }

        SECTION("invalid daily outcome")
        {
            //clear daily outcome by updating it in the next day
            time_t currentTime = txtest::getTestDate(2, 1, 2017);
            statisticsFrame.add(amount, currentTime);
            //try to revert
            REQUIRE_THROWS(statisticsFrame.revert(bigAmount, currentTime, currentTime));
        }

    }


}