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

		enum Result {SUCCESS, LINE_FULL, UNDERFUNDED,
            STATS_OVERFLOW, LIMITS_EXCEEDED};

		AccountManager(Application& app, Database& db, LedgerDelta& delta,
            LedgerManager& lm);

		Result processTransfer(AccountFrame::pointer account, BalanceFrame::pointer balance, int64 amount,
            int64& universalAmount, bool requireReview = false, bool ignoreLimits = false);

        bool revertRequest(AccountFrame::pointer account, BalanceFrame::pointer balance, int64 amount,
            int64 universalAmount, time_t timePerformed);


        bool validateStats(AccountFrame::pointer account, BalanceFrame::pointer balance,
            StatisticsFrame::pointer statsFrame);

        Limits getDefaultLimits(AccountType accountType);
	};
}
