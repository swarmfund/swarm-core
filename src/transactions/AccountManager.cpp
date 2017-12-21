// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include <ledger/AssetFrame.h>
#include <ledger/BalanceHelper.h>
#include "transactions/AccountManager.h"
#include "main/Application.h"
#include "ledger/AssetPairFrame.h"
#include "ledger/LedgerDelta.h"
#include "ledger/AccountLimitsFrame.h"
#include "ledger/AccountLimitsHelper.h"
#include "ledger/AccountTypeLimitsFrame.h"
#include "ledger/AccountTypeLimitsHelper.h"
#include "ledger/AssetPairHelper.h"
#include "ledger/AssetHelper.h"
#include "ledger/EntryHelper.h"
#include "ledger/StatisticsHelper.h"
#include "ledger/FeeHelper.h"

namespace stellar
{
using namespace std;

AccountManager::AccountManager(Application& app, Database& db,
                               LedgerDelta& delta, LedgerManager& lm)
    : mApp(app)
    , mDb(db)
    , mDelta(delta)
    , mLm(lm)
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

AccountManager::Result AccountManager::processTransfer(
    AccountFrame::pointer account, BalanceFrame::pointer balance, int64 amount,
    int64& universalAmount, bool requireReview, bool ignoreLimits)
{
    if (requireReview)
    {
        auto addResult = balance->lockBalance(amount);
        if (addResult == BalanceFrame::Result::UNDERFUNDED)
            return UNDERFUNDED;
        else if (addResult == BalanceFrame::Result::LINE_FULL)
            return LINE_FULL;
    }
    else
    {
        if (!balance->addBalance(-amount))
            return UNDERFUNDED;
    }

    auto assetHelper = AssetHelper::Instance();
    auto statsAssetFrame = assetHelper->loadStatsAsset(mDb);
    if (!statsAssetFrame)
        return SUCCESS;

    auto assetPairHelper = AssetPairHelper::Instance();
    auto assetPairFrame = assetPairHelper->loadAssetPair(balance->getAsset(),
                                                         statsAssetFrame->
                                                         getCode(), mDb,
                                                         &mDelta);
    if (!assetPairFrame)
        return SUCCESS;

    if (!bigDivide(universalAmount, amount, assetPairFrame->getCurrentPrice(),
                   ONE, ROUND_UP))
    {
        return STATS_OVERFLOW;
    }

    auto statisticsHelper = StatisticsHelper::Instance();
    auto stats = statisticsHelper->mustLoadStatistics(balance->getAccountID(),
                                                      mDb, &mDelta);

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

bool AccountManager::revertRequest(AccountFrame::pointer account,
                                   BalanceFrame::pointer balance, int64 amount,
                                   int64 universalAmount, time_t timePerformed)
{
    if (balance->lockBalance(-amount) != BalanceFrame::Result::SUCCESS)
    {
        return false;
    }

    auto statisticsHelper = StatisticsHelper::Instance();
    auto stats = statisticsHelper->mustLoadStatistics(balance->getAccountID(),
                                                      mDb, &mDelta);
    uint64_t now = mLm.getCloseTime();

    auto accIdStrKey = PubKeyUtils::toStrKey(balance->getAccountID());
    if (!stats->add(-universalAmount, now, timePerformed))
    {
        CLOG(ERROR, "AccountManager") <<
            "Failed to revert statistics on revert request";
        throw std::runtime_error("Failed to rever statistics");
    }

    EntryHelperProvider::storeChangeEntry(mDelta, mDb, stats->mEntry);
    return true;
}

bool AccountManager::validateStats(AccountFrame::pointer account,
                                   BalanceFrame::pointer balance,
                                   StatisticsFrame::pointer statsFrame)
{
    auto stats = statsFrame->getStatistics();
    auto accountLimitsHelper = AccountLimitsHelper::Instance();
    auto accountLimits = accountLimitsHelper->
        loadLimits(balance->getAccountID(), mDb);

    Limits limits;

    if (accountLimits)
        limits = accountLimits->getLimits();
    else
    {
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
                                                                  accountType,
                                                                  mDb);
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

bool AccountManager::isFeeMatches(const AccountFrame::pointer account, const Fee fee,
                                  const FeeType feeType, const int64_t subtype, const AssetCode assetCode, const uint64_t amount) const
{
    if (isSystemAccountType(account->getAccountType()))
    {
        return fee.fixed == 0 && fee.percent == 0;
    }
    
    auto feeFrame = FeeHelper::Instance()->loadForAccount(feeType, assetCode, subtype, account, amount, mDb);
    if (!feeFrame)
    {
        return fee.fixed == 0 && fee.percent == 0;
    }

    if (feeFrame->getFee().fixedFee != fee.fixed)
    {
        return false;
    }

    // if we have overflow - fee does not match
    uint64_t expectedPercentFee = 0;
    if (!feeFrame->calculatePercentFee(amount, expectedPercentFee, Rounding::ROUND_UP))
    {
        return false;
    }

    return fee.percent == expectedPercentFee;
}

AccountManager::Result AccountManager::addStats(AccountFrame::pointer account,
                                                BalanceFrame::pointer balance,
                                                uint64_t amountToAdd, uint64_t &universalAmount)
{
    universalAmount = 0;
    auto statsAssetFrame = AssetHelper::Instance()->loadStatsAsset(mDb);
    if (!statsAssetFrame)
        return SUCCESS;

    AssetCode baseAsset = balance->getAsset();
    auto statsAssetPair = AssetPairHelper::Instance()->tryLoadAssetPairForAssets(baseAsset, statsAssetFrame->getCode(), mDb);
    if (!statsAssetFrame)
        return SUCCESS;

    if (!statsAssetPair->convertAmount(statsAssetFrame->getCode(), amountToAdd, ROUND_UP, universalAmount))
        return STATS_OVERFLOW;

    auto statsFrame = StatisticsHelper::Instance()->mustLoadStatistics(account->getID(), mDb);
    time_t currentTime = mLm.getCloseTime();
    if (!statsFrame->add(universalAmount, currentTime, currentTime))
        return STATS_OVERFLOW;

    if (!validateStats(account, balance, statsFrame))
        return LIMITS_EXCEEDED;

    EntryHelperProvider::storeChangeEntry(mDelta, mDb, statsFrame->mEntry);
    return SUCCESS;
}

void AccountManager::revertStats(AccountID account, int64_t universalAmount, time_t timePerformed)
{
    auto statsFrame = StatisticsHelper::Instance()->mustLoadStatistics(account, mDb);
    time_t now = mLm.getCloseTime();
    auto accIdStr = PubKeyUtils::toStrKey(account);
    if (!statsFrame->add(-universalAmount, now, timePerformed)) {
        CLOG(ERROR, "AccountManager") << "Failed to revert statistics on account " << accIdStr;
        throw std::runtime_error("Failed to rever statistics");
    }
    EntryHelperProvider::storeChangeEntry(mDelta, mDb, statsFrame->mEntry);
}

void AccountManager::transferFee(AssetCode asset, uint64_t totalFee)
{
    if (totalFee == 0)
        return;
    // load commission balance and transfer fee
    auto commissionBalance = BalanceHelper::Instance()->loadBalance(mApp.getCommissionID(), asset, mDb, nullptr);
    if (!commissionBalance) {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected state. There is no commission balance for asset " << asset;
        throw std::runtime_error("Unexpected state. Commission balance not found.");
    }

    std::string strBalanceID = PubKeyUtils::toStrKey(commissionBalance->getBalanceID());
    if (!commissionBalance->tryFundAccount(totalFee)) {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Failed to fund commission balance with fee - overflow. balanceID:"
                                               << strBalanceID;
        throw runtime_error("Failed to fund commission balance with fee");
    }

    EntryHelperProvider::storeChangeEntry(mDelta, mDb, commissionBalance->mEntry);
}

void AccountManager::transferFee(AssetCode asset, Fee fee)
{
    uint64_t totalFee = 0;
    if (!safeSum(fee.fixed, fee.percent, totalFee))
        throw std::runtime_error("totalFee overflows uin64");

    transferFee(asset, totalFee);
}

}
