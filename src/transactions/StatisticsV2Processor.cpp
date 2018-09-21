#include <ledger/StatisticsV2Helper.h>
#include <ledger/LimitsV2Helper.h>
#include <ledger/AssetPairHelper.h>
#include <ledger/PendingStatisticsFrame.h>
#include <ledger/PendingStatisticsHelper.h>
#include <ledger/ReviewableRequestHelper.h>
#include "StatisticsV2Processor.h"
#include "ledger/LedgerDelta.h"
#include "ledger/LedgerHeaderFrame.h"


namespace stellar
{

    StatisticsV2Processor::StatisticsV2Processor(Database &db, LedgerDelta &delta, LedgerManager &lm)
            : mDb(db), mDelta(delta), mLm(lm) {}

    bool StatisticsV2Processor::validateStats(LimitsV2Frame::pointer limitsV2Frame,
                                       StatisticsV2Frame::pointer statisticsV2Frame)
    {
        auto statisticsV2 = statisticsV2Frame->getStatistics();
        auto limitsV2 = limitsV2Frame->getLimits();

        if (statisticsV2.dailyOutcome > limitsV2.dailyOut)
            return false;
        if (statisticsV2.weeklyOutcome > limitsV2.weeklyOut)
            return false;
        if (statisticsV2.monthlyOutcome > limitsV2.monthlyOut)
            return false;
        return statisticsV2.annualOutcome <= limitsV2.annualOut;
    }

    StatisticsV2Processor::Result
    StatisticsV2Processor::addStatsV2(SpendType spendType, uint64_t amountToAdd, uint64_t& universalAmount,
                                      AccountFrame::pointer account, BalanceFrame::pointer balance, uint64_t* requestID)
    {
        if (!account || !balance)
        {
            throw std::runtime_error("Unexpected state - expected account and balance not null");
        }

        xdr::pointer<AccountID> accountID = nullptr;
        accountID.activate() = account->getID();

        xdr::pointer<AccountType> accountType = nullptr;
        accountType.activate() = account->getAccountType();

        AssetCode assetCode = balance->getAsset();

        std::vector<StatsOpType> statsOpTypes;
        statsOpTypes.emplace_back(StatsOpType::SPEND);
        switch (spendType)
        {
            case SpendType::WITHDRAW:
                statsOpTypes.emplace_back(StatsOpType::WITHDRAW);
                break;
            case SpendType::PAYMENT:
                statsOpTypes.emplace_back(StatsOpType::PAYMENT_OUT);
                break;
            case SpendType::DEPOSIT:
                statsOpTypes.emplace_back(StatsOpType::DEPOSIT);
                break;
            default:
                throw std::runtime_error("Unexpected spend type");
        }

        auto limitsV2Helper = LimitsV2Helper::Instance();
        auto limitsV2Frames = limitsV2Helper->loadLimits(mDb, statsOpTypes, assetCode, accountID, accountType);

        for (LimitsV2Frame::pointer limitsV2Frame : limitsV2Frames)
        {
            auto statisticsV2Helper = StatisticsV2Helper::Instance();
            auto statisticsV2Frame = statisticsV2Helper->loadStatistics(*accountID, limitsV2Frame->getStatsOpType(),
                    limitsV2Frame->getAsset(), limitsV2Frame->getConvertNeeded(), mDb, &mDelta);

            if (!statisticsV2Frame) {
                auto statisticsV2ID = mDelta.getHeaderFrame().generateID(LedgerEntryType::STATISTICS_V2);
                statisticsV2Frame = StatisticsV2Frame::newStatisticsV2(statisticsV2ID, account->getID(),
                                                                       limitsV2Frame->getStatsOpType(),
                                                                       limitsV2Frame->getAsset(),
                                                                       limitsV2Frame->getConvertNeeded());
                EntryHelperProvider::storeAddEntry(mDelta, mDb, statisticsV2Frame->mEntry);
            }

            universalAmount = amountToAdd;

            if (statisticsV2Frame->getConvertNeeded() && (assetCode != statisticsV2Frame->getAsset()))
            {
                auto statsAssetPair = AssetPairHelper::Instance()->tryLoadAssetPairForAssets(assetCode,
                                                                                       statisticsV2Frame->getAsset(),
                                                                                       mDb);
                if (!statsAssetPair){
                    CLOG(WARNING, Logging::OPERATION_LOGGER) << "Not found such asset pair: " << assetCode << " and "
                                               << statisticsV2Frame->getAsset()
                                               << " while statisticsV2Processor add stats.";
                    continue;
                }

                if (!statsAssetPair->convertAmount(statisticsV2Frame->getAsset(), amountToAdd, ROUND_UP,
                                                   universalAmount))
                    return STATS_V2_OVERFLOW;
            }

            time_t currentTime = mLm.getCloseTime();
            if (!statisticsV2Frame->add(universalAmount, currentTime))
                return STATS_V2_OVERFLOW;

            if (!validateStats(limitsV2Frame, statisticsV2Frame))
                return LIMITS_V2_EXCEEDED;

            EntryHelperProvider::storeChangeEntry(mDelta, mDb, statisticsV2Frame->mEntry);

            if (!requestID)
                continue;

            uint64_t statsID = statisticsV2Frame->getID();
            auto pendingStatisticsFrame = PendingStatisticsFrame::createNew(*requestID, statsID, universalAmount);

            EntryHelperProvider::storeAddEntry(mDelta, mDb, pendingStatisticsFrame->mEntry);
        }

        return SUCCESS;
    }

    void
    StatisticsV2Processor::revertStatsV2(uint64_t requestID)
    {
        auto pendingStatisticsHelper = PendingStatisticsHelper::Instance();
        auto pendingStatisticsVector = pendingStatisticsHelper->loadPendingStatistics(requestID, mDb, mDelta);

        for (PendingStatisticsFrame::pointer pendingStats : pendingStatisticsVector)
        {
            auto statisticsV2Helper = StatisticsV2Helper::Instance();
            auto statisticsV2Frame = statisticsV2Helper->mustLoadStatistics(pendingStats->getStatsID(),
                                                                                        mDb, &mDelta);
            auto reviewableRequestFrame = ReviewableRequestHelper::Instance()->loadRequest(requestID, mDb, &mDelta);
            if (!reviewableRequestFrame)
            {
                throw std::runtime_error("Unexpected state. Expected request to exist, requestID: "
                                         + std::to_string(requestID));
            }

            auto createdAt = reviewableRequestFrame->getCreatedAt();
            auto currentTime = mLm.getCloseTime();
            statisticsV2Frame->revert(pendingStats->getAmount(), currentTime, createdAt);

            statisticsV2Helper->storeChange(mDelta, mDb, statisticsV2Frame->mEntry);

            pendingStatisticsHelper->storeDelete(mDelta, mDb,
                                                 pendingStatisticsHelper->getLedgerKey(pendingStats->mEntry));
        }
    }
}