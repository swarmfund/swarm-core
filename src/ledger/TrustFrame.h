#pragma once

// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ledger/EntryFrame.h"
#include <functional>
#include "map"
#include <unordered_map>
#include "xdr/Stellar-ledger-entries-account.h"

namespace soci
{
class session;
namespace details
{
class prepare_temp_type;
}
}

namespace stellar
{
class LedgerManager;

class TrustFrame : public EntryFrame
{
    void storeUpdate(LedgerDelta& delta, Database& db, bool insert);

    TrustEntry& mTrustEntry;

    TrustFrame(TrustFrame const& from);

    bool isValid();
    

  public:
    typedef std::shared_ptr<TrustFrame> pointer;

    TrustFrame();
    TrustFrame(LedgerEntry const& from);

    EntryFrame::pointer
    copy() const override
    {
        return EntryFrame::pointer(new TrustFrame(*this));
    }



    TrustEntry const&
    getTrust() const
    {
        return mTrustEntry;
    }

    TrustEntry&
    getTrust()
    {
        clearCached();
        return mTrustEntry;
    }

	static pointer createNew(AccountID id, BalanceID balanceToUse);


    // Instance-based overrides of EntryFrame.
    void storeDelete(LedgerDelta& delta, Database& db) const override;
    void storeChange(LedgerDelta& delta, Database& db) override;
    void storeAdd(LedgerDelta& delta, Database& db) override;
    
    // Static helper that don't assume an instance.
    static void storeDelete(LedgerDelta& delta, Database& db,
                            LedgerKey const& key);
    static bool exists(Database& db, LedgerKey const& key);
    static bool exists(Database& db, AccountID allowedAccount, BalanceID balanceToUse);
    static uint64_t countObjects(soci::session& sess);
	static uint64_t countForBalance(Database& db, BalanceID balanceToUser);

    // database utilities
    static TrustFrame::pointer loadTrust(AccountID const& allowedAccount,
        BalanceID const& balanceToUse, Database& db);


    static void dropAll(Database& db);
    static const char* kSQLCreateStatement1;
};
}
