#pragma once

#include "ledger/EntryFrame.h"

namespace  soci
{
    class session;
}

namespace stellar
{
class StatementContext;


class PendingStatisticsFrame : public EntryFrame
{
    PendingStatisticsEntry& mPendingStatistics;

    PendingStatisticsFrame(PendingStatisticsFrame const& from);

public:
    typedef std::shared_ptr<PendingStatisticsFrame> pointer;

    PendingStatisticsFrame();
    PendingStatisticsFrame(LedgerEntry const& from);

    PendingStatisticsFrame& operator=(PendingStatisticsFrame const& other);

    EntryFrame::pointer
    copy() const override
    {
        return EntryFrame::pointer(new PendingStatisticsFrame(*this));
    }

    PendingStatisticsEntry const&
    getPendingStatistics() const
    {
        return mPendingStatistics;
    }

    LedgerVersion getVersion()
    {
        return mPendingStatistics.ext.v();
    }

    uint64_t getStatsID()
    {
        return mPendingStatistics.statisticsID;
    }

    uint64_t getAmount()
    {
        return mPendingStatistics.amount;
    }

    void setAmount(uint64_t amount)
    {
        mPendingStatistics.amount = amount;
    }

    static pointer createNew(uint64& requestID, uint64& statisticsID, uint64& amount);
};

}