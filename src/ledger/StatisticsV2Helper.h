#pragma once
#include "ledger/EntryHelperLegacy.h"
#include "ledger/LedgerManager.h"
#include <functional>
#include <unordered_map>
#include "StatisticsV2Frame.h"

namespace soci
{
    class session;
}

namespace stellar
{
    class StatementContext;

    class StatisticsV2Helper : public EntryHelperLegacy {
    public:
        StatisticsV2Helper(StatisticsV2Helper const&) = delete;
        StatisticsV2Helper& operator= (StatisticsV2Helper const&) = delete;

        static StatisticsV2Helper *Instance() {
            static StatisticsV2Helper singleton;
            return &singleton;
        }

        void dropAll(Database& db) override;
        void storeAdd(LedgerDelta& delta, Database& db, LedgerEntry const& entry) override;
        void storeChange(LedgerDelta& delta, Database& db, LedgerEntry const& entry) override;
        void storeDelete(LedgerDelta& delta, Database& db, LedgerKey const& key) override;
        bool exists(Database& db, LedgerKey const& key) override;
        LedgerKey getLedgerKey(LedgerEntry const& from) override;
        EntryFrame::pointer storeLoad(LedgerKey const& key, Database& db) override;
        EntryFrame::pointer fromXDR(LedgerEntry const& from) override;
        uint64_t countObjects(soci::session& sess) override;

        StatisticsV2Frame::pointer loadStatistics(uint64_t id, Database& db, LedgerDelta* delta = nullptr);
        StatisticsV2Frame::pointer loadStatistics(AccountID& accountID, StatsOpType statsOpType,
                                                  AssetCode const& assetCode, bool isConvertNeeded,
                                                  Database &db, LedgerDelta *delta = nullptr);

        StatisticsV2Frame::pointer mustLoadStatistics(uint64_t id, Database& db, LedgerDelta* delta = nullptr);
        StatisticsV2Frame::pointer mustLoadStatistics(AccountID& accountID, StatsOpType statsOpType,
                                                      AssetCode const& assetCode, bool isConvertNeeded,
                                                      Database &db, LedgerDelta *delta = nullptr);
    private:
        StatisticsV2Helper() { ; }
        ~StatisticsV2Helper() { ; }

        void loadStatistics(StatementContext& prep, std::function<void(LedgerEntry const&)> processor);

        void storeUpdateHelper(LedgerDelta& delta, Database& db, bool insert, LedgerEntry const& entry);

    };
}
