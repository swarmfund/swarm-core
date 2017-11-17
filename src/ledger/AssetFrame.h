#pragma once

// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ledger/EntryFrame.h"
#include "ledger/LedgerManager.h"
#include <functional>
#include <unordered_map>

namespace soci
{
class session;
}

namespace stellar
{
class StatementContext;

class AssetFrame : public EntryFrame
{
    static void
    loadAssets(StatementContext& prep,
               std::function<void(LedgerEntry const&)> AssetProcessor);

    AssetEntry& mAsset;

    AssetFrame(AssetFrame const& from);

    void storeUpdateHelper(LedgerDelta& delta, Database& db, bool insert);

public:
    typedef std::shared_ptr<AssetFrame> pointer;

    AssetFrame();
    AssetFrame(LedgerEntry const& from);

    AssetFrame& operator=(AssetFrame const& other);

	static pointer create(AssetCode code, int32_t policies);

    EntryFrame::pointer
    copy() const override
    {
        return EntryFrame::pointer(new AssetFrame(*this));
    }

    AssetEntry const&
    getAsset() const
    {
        return mAsset;
    }
    AssetEntry&
    getAsset()
    {
        return mAsset;
    }

    int32 getPolicies()
    {
        return mAsset.policies;
    }

    AssetCode getCode()
    {
        return mAsset.code;
    }

    void setPolicies(int32 policies)
    {
        mAsset.policies = policies;
    }
    
    bool checkPolicy(AssetPolicy policy) const
    {
        return (mAsset.policies & policy) == policy;
    }

    static bool isValid(AssetEntry const& oe);
    bool isValid() const;

    // Instance-based overrides of EntryFrame.
    void storeDelete(LedgerDelta& delta, Database& db) const override;
    void storeChange(LedgerDelta& delta, Database& db) override;
    void storeAdd(LedgerDelta& delta, Database& db) override;

    // Static helpers that don't assume an instance.
    static void storeDelete(LedgerDelta& delta, Database& db,
                            LedgerKey const& key);
	static bool exists(Database& db, LedgerKey const& key);
	static bool exists(Database& db, AssetCode code);
    static uint64_t countObjects(soci::session& sess);

    // database utilities
    static pointer loadAsset(AssetCode code,
                             Database& db, LedgerDelta* delta = nullptr);

	static void loadAssets(std::vector<AssetFrame::pointer>& retAssets,
		Database& db);

    static void dropAll(Database& db);
    static const char* kSQLCreateStatement1;
};
}
