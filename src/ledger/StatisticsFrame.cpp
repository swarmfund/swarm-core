// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include <util/types.h>
#include "ledger/StatisticsFrame.h"
#include "database/Database.h"

using namespace std;
using namespace soci;

namespace stellar
{

StatisticsFrame::StatisticsFrame() : EntryFrame(LedgerEntryType::STATISTICS), mStatistics(mEntry.data.stats())
{
}

StatisticsFrame::StatisticsFrame(LedgerEntry const& from)
    : EntryFrame(from), mStatistics(mEntry.data.stats())
{
}

StatisticsFrame::StatisticsFrame(StatisticsFrame const& from) : StatisticsFrame(from.mEntry)
{
}

StatisticsFrame& StatisticsFrame::operator=(StatisticsFrame const& other)
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
StatisticsFrame::isValid(StatisticsEntry const& se)
{
	bool res =  se.dailyOutcome >= 0;
	res = res && (se.monthlyOutcome >= se.dailyOutcome);
	res = res && (se.annualOutcome >= se.annualOutcome); // FIXME useless compare
	return res;
}

bool
StatisticsFrame::isValid() const
{
    return isValid(mStatistics);
}

void StatisticsFrame::clearObsolete(time_t rawCurrentTime)
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

bool StatisticsFrame::add(uint64_t outcome, time_t rawCurrentTime)
{
    clearObsolete(rawCurrentTime);
    mStatistics.updatedAt = rawCurrentTime;

    struct tm currentTime = VirtualClock::tm_from_time_t(rawCurrentTime);

    if (!safeSum(mStatistics.annualOutcome, outcome, mStatistics.annualOutcome))
        return false;

    if (!safeSum(mStatistics.monthlyOutcome, outcome, mStatistics.monthlyOutcome))
        return false;

    if (!safeSum(mStatistics.weeklyOutcome, outcome, mStatistics.weeklyOutcome))
        return false;

    return safeSum(mStatistics.dailyOutcome, outcome, mStatistics.dailyOutcome);
}

void StatisticsFrame::revert(uint64_t outcome, time_t rawCurrentTime, time_t rawTimePerformed)
{
    clearObsolete(rawCurrentTime);
    mStatistics.updatedAt = rawCurrentTime;

    struct tm currentTime = VirtualClock::tm_from_time_t(rawCurrentTime);
    struct tm timePerformed = VirtualClock::tm_from_time_t(rawTimePerformed);

    if (currentTime.tm_year != timePerformed.tm_year)
        return;

    if (outcome > mStatistics.annualOutcome)
        throw std::runtime_error("Unable to revert statistics. Annual outcome can't be negative");

    mStatistics.annualOutcome -= outcome;


    if (currentTime.tm_mon != timePerformed.tm_mon)
        return;

    if (outcome > mStatistics.monthlyOutcome)
        throw std::runtime_error("Unable to revert statistics. Monthly outcome can't be negative");

    mStatistics.monthlyOutcome -= outcome;


    bool weekPassed = VirtualClock::weekPassed(timePerformed, currentTime);
    if (weekPassed)
        return;

    if (outcome > mStatistics.weeklyOutcome)
        throw std::runtime_error("Unable to revert statistics. Weekly outcome can't be negative");

    mStatistics.weeklyOutcome -= outcome;


    if (currentTime.tm_yday != timePerformed.tm_yday)
        return;

    if (outcome > mStatistics.dailyOutcome)
        throw std::runtime_error("Unable to revert statistics. Daily outcome can't be negative");

    mStatistics.dailyOutcome -= outcome;
}

}
