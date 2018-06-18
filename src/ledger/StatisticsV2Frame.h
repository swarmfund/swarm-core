#pragma once

#include "ledger/EntryFrame.h"
#include <functional>
#include <unordered_map>
#include <xdr/Stellar-ledger-entries-statistics-v2.h>

namespace soci
{
    class session;
}

namespace stellar
{
    class StatementContext;

    class StatisticsV2Frame : public EntryFrame
    {
        StatisticsV2Entry& mStatistics;

        StatisticsV2Frame(StatisticsV2Frame const& from);

    public:
        typedef std::shared_ptr<StatisticsV2Frame> pointer;

        StatisticsV2Frame();
        StatisticsV2Frame(LedgerEntry const& from);

        StatisticsV2Frame& operator=(StatisticsV2Frame const& other);

        EntryFrame::pointer
        copy() const override
        {
            return EntryFrame::pointer(new StatisticsV2Frame(*this));
        }

        StatisticsV2Entry const&
        getStatistics() const
        {
            return mStatistics;
        }
        StatisticsV2Entry&
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

        bool getConvertNeeded()
        {
            return mStatistics.isConvertNeeded;
        }

        AssetCode getAsset()
        {
            return mStatistics.assetCode;
        }

        uint64_t getID()
        {
            return mStatistics.id;
        }

        void clearObsolete(time_t rawCurrentTime);
        bool add(uint64_t outcome, time_t currentTime);
        void
        revert(uint64_t outcome, time_t rawCurrentTime, time_t rawTimePerformed);

        static bool isValid(StatisticsV2Entry const& oe);
        bool isValid() const;

        static StatisticsV2Frame::pointer newStatisticsV2(uint64_t id, AccountID const& accountID,
                                                          StatsOpType statsOpType, AssetCode assetCode,
                                                          bool isConvertNeeded);
    };
}

