#include "StatisticsV2Frame.h"
#include <util/types.h>
#include "database/Database.h"

using namespace std;
using namespace soci;

namespace stellar
{

    StatisticsV2Frame::StatisticsV2Frame() : EntryFrame(LedgerEntryType::STATISTICS_V2),
                                             mStatistics(mEntry.data.statisticsV2()) {}

    StatisticsV2Frame::StatisticsV2Frame(LedgerEntry const& from) : EntryFrame(from),
                                                                    mStatistics(mEntry.data.statisticsV2()) {}

    StatisticsV2Frame::StatisticsV2Frame(StatisticsV2Frame const& from) : StatisticsV2Frame(from.mEntry) {}

    StatisticsV2Frame& StatisticsV2Frame::operator=(StatisticsV2Frame const& other)
    {
        if (&other != this)
        {
            mStatistics = other.mStatistics;
            mKey = other.mKey;
            mKeyCalculated = other.mKeyCalculated;
        }
        return *this;
    }

    bool
    StatisticsV2Frame::isValid(StatisticsV2Entry const& se)
    {
        bool res = se.dailyOutcome >= 0;
        res = res && (se.weeklyOutcome >= se.dailyOutcome);
        // there is case when month or year has changed, but week is still the same 
        // so no need to check against them
        res = res && (se.monthlyOutcome >= se.dailyOutcome);
        return res && (se.annualOutcome >= se.monthlyOutcome);
    }

    bool
    StatisticsV2Frame::isValid() const
    {
        return isValid(mStatistics);
    }

    void StatisticsV2Frame::clearObsolete(time_t rawCurrentTime)
    {
        struct tm currentTime = VirtualClock::tm_from_time_t(rawCurrentTime);

        struct tm timeUpdated = VirtualClock::tm_from_time_t(mStatistics.updatedAt);

        bool isYear = timeUpdated.tm_year < currentTime.tm_year;
        if (isYear)
        {
            mStatistics.annualOutcome = 0;
        }

        bool isMonth = isYear || timeUpdated.tm_mon < currentTime.tm_mon;
        if (isMonth)
        {
            mStatistics.monthlyOutcome = 0;
        }

        bool isWeek = VirtualClock::weekPassed(timeUpdated, currentTime);
        if (isWeek)
        {
            mStatistics.weeklyOutcome = 0;
        }

        bool isDay = isYear || timeUpdated.tm_yday < currentTime.tm_yday;
        if (isDay)
        {
            mStatistics.dailyOutcome = 0;
        }
    }

    bool StatisticsV2Frame::add(uint64_t outcome, time_t rawCurrentTime)
    {
        clearObsolete(rawCurrentTime);
        mStatistics.updatedAt = rawCurrentTime;

        if (!safeSum(mStatistics.annualOutcome, outcome, mStatistics.annualOutcome))
            return false;

        if (!safeSum(mStatistics.monthlyOutcome, outcome, mStatistics.monthlyOutcome))
            return false;

        if (!safeSum(mStatistics.weeklyOutcome, outcome, mStatistics.weeklyOutcome))
            return false;

        return safeSum(mStatistics.dailyOutcome, outcome, mStatistics.dailyOutcome);
    }

    void StatisticsV2Frame::revert(uint64_t outcome, time_t rawCurrentTime, time_t rawTimePerformed)
    {
        clearObsolete(rawCurrentTime);
        mStatistics.updatedAt = rawCurrentTime;

        struct tm currentTime = VirtualClock::tm_from_time_t(rawCurrentTime);
        struct tm timePerformed = VirtualClock::tm_from_time_t(rawTimePerformed);

        if (currentTime.tm_year != timePerformed.tm_year)
            return;

        if (outcome > mStatistics.annualOutcome)
            throw std::runtime_error("Unable to revert statisticsV2. Annual outcome can't be negative");

        mStatistics.annualOutcome -= outcome;


        if (currentTime.tm_mon != timePerformed.tm_mon)
            return;

        if (outcome > mStatistics.monthlyOutcome)
            throw std::runtime_error("Unable to revert statisticsV2. Monthly outcome can't be negative");

        mStatistics.monthlyOutcome -= outcome;


        bool weekPassed = VirtualClock::weekPassed(timePerformed, currentTime);
        if (weekPassed)
            return;

        if (outcome > mStatistics.weeklyOutcome)
            throw std::runtime_error("Unable to revert statisticsV2. Weekly outcome can't be negative");

        mStatistics.weeklyOutcome -= outcome;


        if (currentTime.tm_yday != timePerformed.tm_yday)
            return;

        if (outcome > mStatistics.dailyOutcome)
            throw std::runtime_error("Unable to revert statisticsV2. Daily outcome can't be negative");

        mStatistics.dailyOutcome -= outcome;
    }

    StatisticsV2Frame::pointer
    StatisticsV2Frame::newStatisticsV2(uint64_t id, AccountID const& accountID, StatsOpType statsOpType,
                                       AssetCode assetCode, bool isConvertNeeded)
    {
        auto result = new StatisticsV2Frame();
        auto& stats = result->getStatistics();

        stats.id = id;
        stats.accountID = accountID;
        stats.statsOpType = statsOpType;
        stats.assetCode = assetCode;
        stats.isConvertNeeded = isConvertNeeded;
        stats.dailyOutcome = 0;
        stats.weeklyOutcome = 0;
        stats.monthlyOutcome = 0;
        stats.annualOutcome = 0;

        return StatisticsV2Frame::pointer(result);
    }

}
