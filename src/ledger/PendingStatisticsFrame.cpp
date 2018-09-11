#include "PendingStatisticsFrame.h"

using namespace std;
using namespace soci;

namespace stellar
{
    PendingStatisticsFrame::PendingStatisticsFrame() : EntryFrame(LedgerEntryType::PENDING_STATISTICS),
                                                       mPendingStatistics(mEntry.data.pendingStatistics()) {}

    PendingStatisticsFrame::PendingStatisticsFrame(LedgerEntry const& from) : EntryFrame(from),
                                                   mPendingStatistics(mEntry.data.pendingStatistics()) {}

    PendingStatisticsFrame::PendingStatisticsFrame(PendingStatisticsFrame const& from) :
            PendingStatisticsFrame(from.mEntry) {}

    PendingStatisticsFrame& PendingStatisticsFrame::operator=(PendingStatisticsFrame const& other)
    {
        if(&other != this)
        {
            mPendingStatistics = other.mPendingStatistics;
            mKey = other.mKey;
            mKeyCalculated = other.mKeyCalculated;
        }
        return *this;
    }

    PendingStatisticsFrame::pointer
    PendingStatisticsFrame::createNew(stellar::uint64 &requestID, stellar::uint64 &statisticsID,
                                      stellar::uint64 &amount)
    {
        LedgerEntry le;
        le.data.type(LedgerEntryType::PENDING_STATISTICS);
        PendingStatisticsEntry& entry = le.data.pendingStatistics();

        entry.requestID = requestID;
        entry.statisticsID = statisticsID;
        entry.amount = amount;

        return make_shared<PendingStatisticsFrame>(le);
    }
}