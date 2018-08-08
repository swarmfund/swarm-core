#pragma once

// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ledger/EntryHelperLegacy.h"
#include "ledger/LedgerManager.h"
#include <functional>
#include <unordered_map>
#include "AssetFrame.h"

namespace soci
{
class session;
}

namespace stellar
{
class StatementContext;

class AssetHelper : public EntryHelperLegacy
{
public:

    static AssetHelper* Instance()
    {
        static AssetHelper singleton;
        return &singleton;
    }

    void dropAll(Database& db) override;
    void storeAdd(LedgerDelta& delta, Database& db, LedgerEntry const& entry)
    override;
    void storeChange(LedgerDelta& delta, Database& db, LedgerEntry const& entry)
    override;
    void storeDelete(LedgerDelta& delta, Database& db, LedgerKey const& key)
    override;
    bool exists(Database& db, LedgerKey const& key) override;
    LedgerKey getLedgerKey(LedgerEntry const& from) override;
    EntryFrame::pointer storeLoad(LedgerKey const& key, Database& db) override;
    EntryFrame::pointer fromXDR(LedgerEntry const& from) override;
    uint64_t countObjects(soci::session& sess) override;


    AssetFrame::pointer loadAsset(AssetCode code,
                                  Database& db, LedgerDelta* delta = nullptr);

    // mustLoadAsset - loads asset, if not found throws exception
    AssetFrame::pointer mustLoadAsset(AssetCode code,
        Database& db, LedgerDelta* delta = nullptr);


    AssetFrame::pointer loadAsset(AssetCode code, AccountID const& owner,
                                  Database& db, LedgerDelta* delta = nullptr);

    AssetFrame::pointer loadStatsAsset(Database& db);

    void loadAssets(std::vector<AssetFrame::pointer>& retAssets, Database& db);

    void loadBaseAssets(std::vector<AssetFrame::pointer>& retAssets,
                        Database& db);

    bool exists(Database& db, AssetCode code);

private:
    AssetHelper() { ; }
    ~AssetHelper() { ; }

    AssetHelper(AssetHelper const&) = delete;
    AssetHelper& operator=(AssetHelper const&) = delete;

    void loadAssets(StatementContext& prep,
                    std::function<void(LedgerEntry const&)> AssetProcessor);

    void storeUpdateHelper(LedgerDelta& delta, Database& db, bool insert,
                           LedgerEntry const& entry);
};
}
