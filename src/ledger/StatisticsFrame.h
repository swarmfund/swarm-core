#pragma once

// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ledger/EntryFrame.h"
#include <functional>
#include <unordered_map>

namespace soci
{
class session;
}

namespace stellar
{
class StatementContext;

class StatisticsFrame : public EntryFrame
{
    StatisticsEntry& mStatistics;

    StatisticsFrame(StatisticsFrame const& from);

  public:
    typedef std::shared_ptr<StatisticsFrame> pointer;

    StatisticsFrame();
    StatisticsFrame(LedgerEntry const& from);

    StatisticsFrame& operator=(StatisticsFrame const& other);

    EntryFrame::pointer
    copy() const override
    {
        return EntryFrame::pointer(new StatisticsFrame(*this));
    }

    StatisticsEntry const&
    getStatistics() const
    {
        return mStatistics;
    }
    StatisticsEntry&
    getStatistics()
    {
        return mStatistics;
    }

    uint64_t getDailyOutcome(){
        return mStatistics.dailyOutcome;
    }

    uint64_t getWeeklyOutcome() {
        return mStatistics.weeklyOutcome;
    }

    uint64_t getMonthlyOutcome() {
        return mStatistics.monthlyOutcome;
    }

    uint64_t getAnnualOutcome() {
        return mStatistics.annualOutcome;
    }

    int64 getUpdateAt() {
        return mStatistics.updatedAt;
    }

    LedgerVersion getVersion(){
        return mStatistics.ext.v();
    }

    AccountID getAccountID(){
        return mStatistics.accountID;
    }

	void clearObsolete(time_t rawCurrentTime);
	bool add(uint64_t outcome, time_t currentTime);
    void
    revert(uint64_t outcome, time_t rawCurrentTime, time_t rawTimePerformed);

    static bool isValid(StatisticsEntry const& oe);
    bool isValid() const;
};
}
