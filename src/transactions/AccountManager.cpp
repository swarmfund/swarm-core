// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "transactions/AccountManager.h"
#include "main/Application.h"
#include "ledger/AssetPairFrame.h"
#include "ledger/LedgerDelta.h"
#include "ledger/AccountLimitsFrame.h"
#include "ledger/AccountLimitsHelper.h"
#include "ledger/AccountTypeLimitsFrame.h"
#include "ledger/AccountTypeLimitsHelper.h"
#include "ledger/AssetPairHelper.h"
#include "ledger/EntryHelper.h"
#include "ledger/StatisticsHelper.h"

namespace stellar
{

	using namespace std;

	AccountManager::AccountManager(Application& app, Database& db, LedgerDelta& delta, LedgerManager &lm)
		: mApp(app), mDb(db), mDelta(delta), mLm(lm)
	{
	}

	void AccountManager::createStats(AccountFrame::pointer account)
	{
		auto statsFrame = make_shared<StatisticsFrame>();
		auto& stats = statsFrame->getStatistics();
		stats.accountID = account->getID();
        stats.dailyOutcome = 0;
        stats.weeklyOutcome = 0;
        stats.monthlyOutcome = 0;
        stats.annualOutcome = 0;
		EntryHelperProvider::storeAddEntry(mDelta, mDb, statsFrame->mEntry);
	}

	AccountManager::Result AccountManager::processTransfer(AccountFrame::pointer account, BalanceFrame::pointer balance, int64 amount, int64& universalAmount, bool requireReview, bool ignoreLimits)
	{
        if (requireReview) {
            auto addResult = balance->lockBalance(amount);
            if (addResult == BalanceFrame::Result::UNDERFUNDED)
                return UNDERFUNDED;
            else if (addResult == BalanceFrame::Result::LINE_FULL)
                return LINE_FULL;
        }
        else {
            if (!balance->addBalance(-amount))
                return UNDERFUNDED;
        }

		auto assetPairHelper = AssetPairHelper::Instance();
        auto assetPairFrame = assetPairHelper->mustLoadAssetPair(balance->getAsset(), mApp.getStatsQuoteAsset(), mDb, &mDelta);

        if (!bigDivide(universalAmount, amount, assetPairFrame->getCurrentPrice(),
            ONE, ROUND_UP))
        {
            return STATS_OVERFLOW;
        }

		auto statisticsHelper = StatisticsHelper::Instance();
		auto stats = statisticsHelper->mustLoadStatistics(balance->getAccountID(), mDb, &mDelta);

		auto now = mLm.getCloseTime();		
		if (!stats->add(universalAmount, now, now))
		{
			return STATS_OVERFLOW;
		}
        
        if (!ignoreLimits && !validateStats(account, balance, stats))
		{
			return LIMITS_EXCEEDED;
		}
        
        EntryHelperProvider::storeChangeEntry(mDelta, mDb, stats->mEntry);
		return SUCCESS;
	}
    
    bool AccountManager::revertRequest(AccountFrame::pointer account, BalanceFrame::pointer balance, int64 amount,
        int64 universalAmount, time_t timePerformed)
    {
        if (balance->lockBalance(-amount) != BalanceFrame::Result::SUCCESS)
        {
            return false;
        }

		auto statisticsHelper = StatisticsHelper::Instance();
		auto stats = statisticsHelper->mustLoadStatistics(balance->getAccountID(), mDb, &mDelta);
		uint64_t now = mLm.getCloseTime();

		auto accIdStrKey = PubKeyUtils::toStrKey(balance->getAccountID());
		if (!stats->add(-universalAmount, now, timePerformed))
		{
			CLOG(ERROR, "AccountManager") << "Failed to revert statistics on revert request";
			throw std::runtime_error("Failed to rever statistics");
		}

        EntryHelperProvider::storeChangeEntry(mDelta, mDb, stats->mEntry);
        return true;
	}
        
    bool AccountManager::validateStats(AccountFrame::pointer account, BalanceFrame::pointer balance,
        StatisticsFrame::pointer statsFrame) {
        auto stats = statsFrame->getStatistics();
		auto accountLimitsHelper = AccountLimitsHelper::Instance();
        auto accountLimits = accountLimitsHelper->loadLimits(balance->getAccountID(), mDb);
        
        Limits limits;
        
        if (accountLimits)
            limits = accountLimits->getLimits();
        else {
            limits = getDefaultLimits(account->getAccountType());
        }
        if (stats.dailyOutcome > limits.dailyOut)
            return false;
        if (stats.weeklyOutcome > limits.weeklyOut)
            return false;
        if (stats.monthlyOutcome > limits.monthlyOut)
            return false;
        if (stats.annualOutcome > limits.annualOut)
            return false;
        return true;
    }

    Limits AccountManager::getDefaultLimits(AccountType accountType)
    {
		auto accountTypeLimitsHelper = AccountTypeLimitsHelper::Instance();
		auto defaultLimitsFrame = accountTypeLimitsHelper->loadLimits(
			accountType, mDb);
        Limits limits;

        if (defaultLimitsFrame)
            limits = defaultLimitsFrame->getLimits();
        else
        {
            limits.dailyOut = INT64_MAX;
            limits.weeklyOut = INT64_MAX;
            limits.monthlyOut = INT64_MAX;
            limits.annualOut = INT64_MAX;
        }
        return limits;
    }

}
