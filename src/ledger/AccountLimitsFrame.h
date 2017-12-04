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
    AccountLimitsEntry& mAccountLimits;

    AccountLimitsFrame(AccountLimitsFrame const& from);

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
    
    static AccountLimitsFrame::pointer createNew(AccountID accountID, Limits limits);

};
}
