#pragma once

// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ledger/EntryFrame.h"
#include <functional>
#include <unordered_map>

namespace soci
{
class session;
}

namespace stellar
{
class StatementContext;

class AccountTypeLimitsFrame : public EntryFrame
{
    static void
    loadAccountTypeLimits(StatementContext& prep,
               std::function<void(LedgerEntry const&)> AccountTypeLimitsProcessor);

    AccountTypeLimitsEntry& mAccountTypeLimits;

    AccountTypeLimitsFrame(AccountTypeLimitsFrame const& from);

    void storeUpdateHelper(LedgerDelta& delta, Database& db, bool insert);

  public:
    typedef std::shared_ptr<AccountTypeLimitsFrame> pointer;

    AccountTypeLimitsFrame();
    AccountTypeLimitsFrame(LedgerEntry const& from);

    AccountTypeLimitsFrame& operator=(AccountTypeLimitsFrame const& other);

    EntryFrame::pointer
    copy() const override
    {
        return EntryFrame::pointer(new AccountTypeLimitsFrame(*this));
    }

    AccountTypeLimitsEntry const&
    getAccountTypeLimits() const
    {
        return mAccountTypeLimits;
    }
    AccountTypeLimitsEntry&
    getAccountTypeLimits()
    {
        return mAccountTypeLimits;
    }

    Limits
    getLimits()
    {
        return mAccountTypeLimits.limits;
    }
    
    void setLimits(Limits limits)
    {
        mAccountTypeLimits.limits = limits;
    }
    

    static bool isValid(AccountTypeLimitsEntry const& oe);
    bool isValid() const;

    // Instance-based overrides of EntryFrame.
    void storeDelete(LedgerDelta& delta, Database& db) const override;
    void storeChange(LedgerDelta& delta, Database& db) override;
    void storeAdd(LedgerDelta& delta, Database& db) override;

    // Static helpers that don't assume an instance.
    static void storeDelete(LedgerDelta& delta, Database& db,
                            LedgerKey const& key);
	
    static AccountTypeLimitsFrame::pointer loadLimits(AccountType accountType, Database& db, LedgerDelta* delta = nullptr);
    
    static bool exists(Database& db, LedgerKey const& key);
    static uint64_t countObjects(soci::session& sess);

    // database utilities

    static void dropAll(Database& db);
    static const char* kSQLCreateStatement1;
};
}
