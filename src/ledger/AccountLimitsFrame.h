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

class AccountLimitsFrame : public EntryFrame
{
    static void
    loadAccountLimits(StatementContext& prep,
               std::function<void(LedgerEntry const&)> AccountLimitsProcessor);

    AccountLimitsEntry& mAccountLimits;

    AccountLimitsFrame(AccountLimitsFrame const& from);

    void storeUpdateHelper(LedgerDelta& delta, Database& db, bool insert);

  public:
    typedef std::shared_ptr<AccountLimitsFrame> pointer;

    AccountLimitsFrame();
    AccountLimitsFrame(LedgerEntry const& from);

    AccountLimitsFrame& operator=(AccountLimitsFrame const& other);

    EntryFrame::pointer
    copy() const override
    {
        return EntryFrame::pointer(new AccountLimitsFrame(*this));
    }

    AccountLimitsEntry const&
    getAccountLimits() const
    {
        return mAccountLimits;
    }
    AccountLimitsEntry&
    getAccountLimits()
    {
        return mAccountLimits;
    }

    Limits
    getLimits()
    {
        return mAccountLimits.limits;
    }
    
    void setLimits(Limits limits)
    {
        mAccountLimits.limits = limits;
    }
    

    static bool isValid(AccountLimitsEntry const& oe);
    bool isValid() const;

    // Instance-based overrides of EntryFrame.
    void storeDelete(LedgerDelta& delta, Database& db) const override;
    void storeChange(LedgerDelta& delta, Database& db) override;
    void storeAdd(LedgerDelta& delta, Database& db) override;

    // Static helpers that don't assume an instance.
    static void storeDelete(LedgerDelta& delta, Database& db,
                            LedgerKey const& key);
	
    static AccountLimitsFrame::pointer loadLimits(AccountID accountID,
        Database& db, LedgerDelta* delta = nullptr);
    
    static AccountLimitsFrame::pointer createNew(AccountID accountID, Limits limits);
    
    static bool exists(Database& db, LedgerKey const& key);
    static uint64_t countObjects(soci::session& sess);

    // database utilities

    static void dropAll(Database& db);
    static const char* kSQLCreateStatement1;
};
}
