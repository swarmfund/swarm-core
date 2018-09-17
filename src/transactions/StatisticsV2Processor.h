#pragma once

#include <transactions/AccountManager.h>
#include "ledger/LimitsV2Frame.h"
#include "ledger/StatisticsV2Frame.h"

namespace stellar
{

class StatisticsV2Processor {

    Database& mDb;
    LedgerDelta& mDelta;
    LedgerManager& mLm;

public:
    StatisticsV2Processor(Database& db, LedgerDelta& delta, LedgerManager& lm);

    enum Result
    {
        SUCCESS,
        STATS_V2_OVERFLOW,
        LIMITS_V2_EXCEEDED
    };

    enum SpendType
    {
        PAYMENT,
        WITHDRAW,
        DEPOSIT,
        PAYOUT,
    };

    StatisticsV2Processor::Result addStatsV2(SpendType spendType, uint64_t amountToAdd, uint64_t& universalAmount,
                                             AccountFrame::pointer account = nullptr,
                                             BalanceFrame::pointer balance = nullptr,
                                             uint64_t* requestID = nullptr);

    void revertStatsV2(uint64_t requestID);

    bool validateStats(LimitsV2Frame::pointer limitsV2Frame, StatisticsV2Frame::pointer statisticsV2Frame);

};

}