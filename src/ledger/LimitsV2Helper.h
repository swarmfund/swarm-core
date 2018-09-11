#pragma once

#include "EntryHelperLegacy.h"
#include <functional>
#include <unordered_map>
#include "AccountLimitsFrame.h"
#include "LimitsV2Frame.h"
#include "AccountFrame.h"
#include "BalanceFrame.h"

namespace soci
{
    class session;
}

namespace stellar
{
class StatementContext;


class LimitsV2Helper : public EntryHelperLegacy
{
public:
    static LimitsV2Helper *Instance() {
        static LimitsV2Helper singleton;
        return&singleton;
    }

    LimitsV2Helper(LimitsV2Helper const&) = delete;
    LimitsV2Helper& operator=(LimitsV2Helper const&) = delete;

    void dropAll(Database& db) override;
    void storeAdd(LedgerDelta& delta, Database& db, LedgerEntry const& entry) override;
    void storeChange(LedgerDelta& delta, Database& db, LedgerEntry const& entry) override;
    void storeDelete(LedgerDelta& delta, Database& db, LedgerKey const& key) override;
    bool exists(Database& db, LedgerKey const& key) override;
    LedgerKey getLedgerKey(LedgerEntry const& from) override;
    EntryFrame::pointer storeLoad(LedgerKey const& key, Database& db) override;
    EntryFrame::pointer fromXDR(LedgerEntry const& from) override;
    uint64_t countObjects(soci::session& sess) override;


    std::vector<LimitsV2Frame::pointer> loadLimits(Database &db, std::vector<StatsOpType> statsOpTypes,
                                      AssetCode assetCode, xdr::pointer<AccountID> accountID = nullptr,
                                      xdr::pointer<AccountType> accountType = nullptr);
    LimitsV2Frame::pointer loadLimits(Database &db, StatsOpType statsOpType, AssetCode assetCode,
                                      xdr::pointer<AccountID> accountID, xdr::pointer<AccountType> accountType,
                                      bool isConvertNeeded, LedgerDelta *delta = nullptr);
    LimitsV2Frame::pointer loadLimits(uint64_t id, Database& db, LedgerDelta* delta = nullptr);

private:
    LimitsV2Helper() { ; }
    ~LimitsV2Helper() { ; }

    std::string obtainSqlStatsOpTypesString(std::vector<StatsOpType> stats);
    void load(StatementContext &prep, std::function<void(LedgerEntry const &)> processor);
    void storeUpdateHelper(LedgerDelta& delta, Database& db, bool insert, LedgerEntry const& entry);
};

}
