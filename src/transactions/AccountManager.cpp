// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include <ledger/AssetFrame.h>
#include <ledger/BalanceHelper.h>
#include <ledger/BalanceFrame.h>
#include <ledger/AccountHelper.h>
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
#include "ledger/EntryHelperLegacy.h"
#include "ledger/LedgerHeaderFrame.h"
#include "ledger/StatisticsHelper.h"
#include "ledger/FeeHelper.h"
#include "StatisticsV2Processor.h"

namespace stellar {
    using namespace std;

    AccountManager::AccountManager(Application &app, Database &db,
                                   LedgerDelta &delta, LedgerManager &lm)
            : mApp(app), mDb(db), mDelta(delta), mLm(lm) {
    }

    void AccountManager::createStats(AccountFrame::pointer account) {
        auto statsFrame = make_shared<StatisticsFrame>();
        auto &stats = statsFrame->getStatistics();
        stats.accountID = account->getID();
        stats.dailyOutcome = 0;
        stats.weeklyOutcome = 0;
        stats.monthlyOutcome = 0;
        stats.annualOutcome = 0;
        EntryHelperProvider::storeAddEntry(mDelta, mDb, statsFrame->mEntry);
    }

    AccountManager::Result AccountManager::processTransfer(
            AccountFrame::pointer account, BalanceFrame::pointer balance, int64 amount,
            int64 &universalAmount, bool requireReview, bool ignoreLimits) {
        if (requireReview) {
            auto addResult = balance->lockBalance(amount);
            if (addResult == BalanceFrame::Result::UNDERFUNDED)
                return UNDERFUNDED;
            else if (addResult == BalanceFrame::Result::LINE_FULL)
                return LINE_FULL;
        } else {
            if (!balance->addBalance(-amount))
                return UNDERFUNDED;
        }

        auto statsAssetFrame = AssetHelper::Instance()->loadStatsAsset(mDb);
        if (!statsAssetFrame)
            return SUCCESS;

        auto assetPairFrame = AssetPairHelper::Instance()->loadAssetPair(balance->getAsset(),
                                                                         statsAssetFrame->getCode(),
                                                                         mDb, &mDelta);
        if (!assetPairFrame)
            return SUCCESS;

        if (!bigDivide(universalAmount, amount, assetPairFrame->getCurrentPrice(),
                       ONE, ROUND_UP)) {
            return STATS_OVERFLOW;
        }

        auto stats = StatisticsHelper::Instance()->mustLoadStatistics(balance->getAccountID(), mDb, &mDelta);

        auto now = mLm.getCloseTime();
        if (!stats->add(universalAmount, now)) {
            return STATS_OVERFLOW;
        }

        if (!ignoreLimits && !validateStats(account, balance, stats)) {
            return LIMITS_EXCEEDED;
        }

        EntryHelperProvider::storeChangeEntry(mDelta, mDb, stats->mEntry);
        return SUCCESS;
    }

    bool AccountManager::calculateUniversalAmount(AssetCode transferAsset, uint64_t amount, uint64_t &universalAmount) {
        universalAmount = 0;

        auto statsAssetFrame = AssetHelper::Instance()->loadStatsAsset(mDb);
        if (!statsAssetFrame) {
            return true;
        }

        if (transferAsset == statsAssetFrame->getCode()) {
            universalAmount = amount;
            return true;
        }

        auto assetPairFrame = AssetPairHelper::Instance()->tryLoadAssetPairForAssets(transferAsset,
                                                                                     statsAssetFrame->getCode(),
                                                                                     mDb, &mDelta);
        if (!assetPairFrame) {
            return true;
        }

        return assetPairFrame->convertAmount(transferAsset, amount, Rounding::ROUND_UP, universalAmount);
    }

    AccountManager::ProcessTransferResult
    AccountManager::processTransferV2(AccountFrame::pointer from, BalanceFrame::pointer fromBalance,
                                      BalanceFrame::pointer toBalance, uint64_t amount, bool noIncludeIntoStats) {
        // charge sender
        if (!fromBalance->tryCharge(amount)) {
            return ProcessTransferResult(Result::UNDERFUNDED, 0);
        }

        EntryHelperProvider::storeChangeEntry(mDelta, mDb, fromBalance->mEntry);

        if (!toBalance->tryFundAccount(amount)) {
            return ProcessTransferResult(Result::LINE_FULL, 0);
        }

        EntryHelperProvider::storeChangeEntry(mDelta, mDb, toBalance->mEntry);

        uint64_t universalAmount = 0;
        if (!calculateUniversalAmount(fromBalance->getAsset(), amount, universalAmount)) {
            return ProcessTransferResult(Result::STATS_OVERFLOW, 0);
        }
        if (universalAmount == 0) {
            return ProcessTransferResult(Result::SUCCESS, 0);
        }

        auto result = ProcessTransferResult(Result::SUCCESS, universalAmount);
        if (noIncludeIntoStats) {
            return result;
        }

        return processStatistics(from, fromBalance, amount, universalAmount);
    }

    AccountManager::ProcessTransferResult
    AccountManager::processStatistics(AccountFrame::pointer from, BalanceFrame::pointer fromBalance,
                                      uint64_t amount, uint64_t& universalAmount)
    {
        if (!mLm.shouldUse(LedgerVersion::CREATE_ONLY_STATISTICS_V2))
        {
            auto stats = StatisticsHelper::Instance()->mustLoadStatistics(fromBalance->getAccountID(), mDb, &mDelta);

            auto now = mLm.getCloseTime();
            if (!stats->add(universalAmount, now)) {
                return ProcessTransferResult(Result::STATS_OVERFLOW, 0);
            }

            if (!validateStats(from, fromBalance, stats)) {
                return ProcessTransferResult(Result::LIMITS_EXCEEDED, 0);
            }

            EntryHelperProvider::storeChangeEntry(mDelta, mDb, stats->mEntry);
        }

        universalAmount = 0;
        auto statsV2Result = tryAddStatsV2(from, fromBalance, amount, universalAmount);

        return ProcessTransferResult(statsV2Result, universalAmount);
    }

    bool AccountManager::revertRequest(AccountFrame::pointer account,
                                       BalanceFrame::pointer balance, int64 amount,
                                       int64 universalAmount, time_t timePerformed) {
        if (balance->lockBalance(-amount) != BalanceFrame::Result::SUCCESS) {
            return false;
        }

        auto statisticsHelper = StatisticsHelper::Instance();
        auto stats = statisticsHelper->mustLoadStatistics(balance->getAccountID(),
                                                          mDb, &mDelta);
        uint64_t now = mLm.getCloseTime();

        auto accIdStrKey = PubKeyUtils::toStrKey(balance->getAccountID());
        stats->revert(universalAmount, now, timePerformed);

        EntryHelperProvider::storeChangeEntry(mDelta, mDb, stats->mEntry);
        return true;
    }

    AccountManager::Result
    AccountManager::tryAddStatsV2(const AccountFrame::pointer account,
                                  const BalanceFrame::pointer balance, const uint64_t amountToAdd,
                                  uint64_t& universalAmount)
    {
        StatisticsV2Processor statisticsV2Processor(mDb, mDelta, mLm);
        const auto result = statisticsV2Processor.addStatsV2(StatisticsV2Processor::SpendType::PAYMENT, amountToAdd,
                                                             universalAmount, account, balance);
        switch (result)
        {
            case StatisticsV2Processor::SUCCESS:
                return AccountManager::SUCCESS;
            case StatisticsV2Processor::STATS_V2_OVERFLOW:
                return AccountManager::STATS_OVERFLOW;
            case StatisticsV2Processor::LIMITS_V2_EXCEEDED:
                return AccountManager::LIMITS_EXCEEDED;
            default:
                CLOG(ERROR, Logging::OPERATION_LOGGER)
                        << "Unexpected result from statisticsV2Processor when updating statsV2:" << result;
                throw std::runtime_error("Unexpected state from statisticsV2Processor when updating statsV2");
        }

    }

    bool AccountManager::validateStats(AccountFrame::pointer account,
                                       BalanceFrame::pointer balance,
                                       StatisticsFrame::pointer statsFrame) {
        auto stats = statsFrame->getStatistics();
        auto accountLimitsHelper = AccountLimitsHelper::Instance();
        auto accountLimits = accountLimitsHelper->
                loadLimits(balance->getAccountID(), mDb);

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

    Limits AccountManager::getDefaultLimits(AccountType accountType) {
        auto accountTypeLimitsHelper = AccountTypeLimitsHelper::Instance();
        auto defaultLimitsFrame = accountTypeLimitsHelper->loadLimits(
                accountType,
                mDb);
        Limits limits;

        if (defaultLimitsFrame)
            limits = defaultLimitsFrame->getLimits();
        else {
            limits.dailyOut = INT64_MAX;
            limits.weeklyOut = INT64_MAX;
            limits.monthlyOut = INT64_MAX;
            limits.annualOut = INT64_MAX;
        }
        return limits;
    }

    bool AccountManager::isFeeMatches(const AccountFrame::pointer account, const Fee fee,
                                      const FeeType feeType, const int64_t subtype, const AssetCode assetCode,
                                      const uint64_t amount) const {
        if (isSystemAccountType(account->getAccountType())) {
            return fee.fixed == 0 && fee.percent == 0;
        }

        auto feeFrame = FeeHelper::Instance()->loadForAccount(feeType, assetCode, subtype, account, amount, mDb);
        if (!feeFrame) {
            return fee.fixed == 0 && fee.percent == 0;
        }

        if (feeFrame->getFee().fixedFee != fee.fixed) {
            return false;
        }

        // if we have overflow - fee does not match
        uint64_t expectedPercentFee = 0;
        if (!feeFrame->calculatePercentFee(amount, expectedPercentFee, Rounding::ROUND_UP)) {
            return false;
        }

        return fee.percent == expectedPercentFee;
    }

    AccountManager::Result AccountManager::addStats(AccountFrame::pointer account,
                                                    BalanceFrame::pointer balance,
                                                    uint64_t amountToAdd, uint64_t &universalAmount) {
        universalAmount = 0;
        auto statsAssetFrame = AssetHelper::Instance()->loadStatsAsset(mDb);
        if (!statsAssetFrame)
            return SUCCESS;

        AssetCode baseAsset = balance->getAsset();
        auto statsAssetPair = AssetPairHelper::Instance()->tryLoadAssetPairForAssets(baseAsset,
                                                                                     statsAssetFrame->getCode(), mDb);
        if (!statsAssetPair)
            return SUCCESS;

        if (!statsAssetPair->convertAmount(statsAssetFrame->getCode(), amountToAdd, ROUND_UP, universalAmount))
            return STATS_OVERFLOW;

        auto statsFrame = StatisticsHelper::Instance()->mustLoadStatistics(account->getID(), mDb);
        time_t currentTime = mLm.getCloseTime();
        if (!statsFrame->add(universalAmount, currentTime))
            return STATS_OVERFLOW;

        if (!validateStats(account, balance, statsFrame))
            return LIMITS_EXCEEDED;

        EntryHelperProvider::storeChangeEntry(mDelta, mDb, statsFrame->mEntry);
        return SUCCESS;
    }

    void AccountManager::revertStats(AccountID account, uint64_t universalAmount, time_t timePerformed) {
        auto statsFrame = StatisticsHelper::Instance()->mustLoadStatistics(account, mDb);
        time_t now = mLm.getCloseTime();
        auto accIdStr = PubKeyUtils::toStrKey(account);

        statsFrame->revert(universalAmount, now, timePerformed);

        EntryHelperProvider::storeChangeEntry(mDelta, mDb, statsFrame->mEntry);
    }

    void AccountManager::transferFee(AssetCode asset, uint64_t totalFee) {
        if (totalFee == 0)
            return;

        // load commission balance and transfer fee
        auto commissionBalance = loadOrCreateBalanceFrameForAsset(mApp.getCommissionID(), asset, mDb, mDelta);

        if (!commissionBalance->tryFundAccount(totalFee)) {
            std::string strBalanceID = PubKeyUtils::toStrKey(commissionBalance->getBalanceID());
            CLOG(ERROR, Logging::OPERATION_LOGGER)
                    << "Failed to fund commission balance with fee - overflow. balanceID:"
                    << strBalanceID;
            throw runtime_error("Failed to fund commission balance with fee");
        }

        EntryHelperProvider::storeChangeEntry(mDelta, mDb, commissionBalance->mEntry);
    }

    void AccountManager::transferFee(AssetCode asset, Fee fee) {
        uint64_t totalFee = 0;
        if (!safeSum(fee.fixed, fee.percent, totalFee))
            throw std::runtime_error("totalFee overflows uin64");

        transferFee(asset, totalFee);
    }

    BalanceID AccountManager::loadOrCreateBalanceForAsset(AccountID const &account,
                                                          AssetCode const &asset) const {
        return loadOrCreateBalanceForAsset(account, asset, mDb, mDelta);
    }

    BalanceID AccountManager::loadOrCreateBalanceForAsset(AccountID const &account,
                                                          AssetCode const &asset, Database &db, LedgerDelta &delta) {
        auto balance = loadOrCreateBalanceFrameForAsset(account, asset, db, delta);
        return balance->getBalanceID();
    }

    BalanceFrame::pointer AccountManager::loadOrCreateBalanceFrameForAsset(
            AccountID const &account, AssetCode const &asset, Database &db,
            LedgerDelta &delta) {
        auto balance = BalanceHelper::Instance()->loadBalance(account, asset, db, &delta);
        if (!!balance) {
            return balance;
        }

        if (!AssetHelper::Instance()->exists(db, asset)) {
            CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected db state: expected asset to exist: " << asset;
            throw runtime_error("Unexpected db state: expected asset to exist");
        }

        auto newBalanceID = BalanceKeyUtils::forAccount(account,
                                                        delta.getHeaderFrame().generateID(LedgerEntryType::BALANCE));
        balance = BalanceFrame::createNew(newBalanceID, account, asset);
        EntryHelperProvider::storeAddEntry(delta, db, balance->mEntry);
        return balance;
    }

    AccountManager::Result AccountManager::isAllowedToReceive(BalanceID balanceID, Database& db){
        auto balanceFrame = BalanceHelper::Instance()->loadBalance(balanceID, db);
        if (!balanceFrame)
            return AccountManager::Result::BALANCE_NOT_FOUND;

        return isAllowedToReceive(balanceFrame, db);
    }

    AccountManager::Result  AccountManager::isAllowedToReceive(BalanceFrame::pointer balanceFrame, Database& db){
        auto accountID = balanceFrame->getAccountID();
        auto accountFrame = AccountHelper::Instance()->mustLoadAccount(accountID, db);
        return isAllowedToReceive(accountFrame, balanceFrame, db);
    }
    AccountManager::Result  AccountManager::isAllowedToReceive(AccountFrame::pointer account, BalanceFrame::pointer balance, Database& db){
        auto asset = AssetHelper::Instance()->mustLoadAsset(balance->getAsset(), db);

        if (asset->isRequireVerification() && account->getAccountType() == AccountType::NOT_VERIFIED)
            return AccountManager::Result::REQUIRED_VERIFICATION;

        if (asset->isRequireKYC()){
            if (account->getAccountType() == AccountType::NOT_VERIFIED ||
            account->getAccountType() == AccountType::VERIFIED)
                return AccountManager::Result::REQUIRED_KYC;
        }

        return AccountManager::Result::SUCCESS;
    }

    void AccountManager::unlockPendingIssuanceForSale(SaleFrame::pointer const sale, LedgerDelta &delta, Database &db,
                                                      LedgerManager &lm) {
        auto baseAsset = AssetHelper::Instance()->mustLoadAsset(sale->getBaseAsset(), db, &delta);
        const auto baseAmount = lm.shouldUse(LedgerVersion::ALLOW_TO_SPECIFY_REQUIRED_BASE_ASSET_AMOUNT_FOR_HARD_CAP)
                                ? sale->getSaleEntry().maxAmountToBeSold : baseAsset->getPendingIssuance();
        baseAsset->mustUnlockIssuedAmount(baseAmount);
        AssetHelper::Instance()->storeChange(delta, db, baseAsset->mEntry);
    }
}
