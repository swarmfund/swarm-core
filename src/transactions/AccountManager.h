#pragma once

// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "overlay/StellarXDR.h"
#include "ledger/StatisticsFrame.h"
#include "ledger/AccountFrame.h"
#include "ledger/BalanceFrame.h"


namespace medida
{
class MetricsRegistry;
}

namespace stellar
{
class Application;
class Database;

class AccountManager
{
protected:
    Application& mApp;
    Database& mDb;
    LedgerDelta& mDelta;
    LedgerManager& mLm;

private:
public:
    void createStats(AccountFrame::pointer account);

    enum Result
    {
        SUCCESS,
        LINE_FULL,
        UNDERFUNDED,
        STATS_OVERFLOW,
        LIMITS_EXCEEDED
    };

    AccountManager(Application& app, Database& db, LedgerDelta& delta,
                   LedgerManager& lm);

    Result processTransfer(AccountFrame::pointer account,
                           BalanceFrame::pointer balance, int64 amount,
                           int64& universalAmount, bool requireReview = false,
                           bool ignoreLimits = false);

    bool revertRequest(AccountFrame::pointer account,
                       BalanceFrame::pointer balance, int64 amount,
                       int64 universalAmount, time_t timePerformed);


    bool validateStats(AccountFrame::pointer account,
                       BalanceFrame::pointer balance,
                       StatisticsFrame::pointer statsFrame);

    Limits getDefaultLimits(AccountType accountType);

    bool isFeeMatches(AccountFrame::pointer account, Fee fee, FeeType feeType, int64_t subtype, AssetCode assetCode, uint64_t amount) const;

    Result addStats(AccountFrame::pointer account, BalanceFrame::pointer balance, uint64_t amountToAdd,
                    uint64_t &universalAmount);

    void revertStats(AccountID account, uint64_t universalAmount, time_t timePerformed);

    void transferFee(AssetCode asset, uint64_t totalFee);

    void transferFee(AssetCode asset, Fee fee);
};
}
