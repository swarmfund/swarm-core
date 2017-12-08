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
    AccountTypeLimitsEntry& mAccountTypeLimits;

    AccountTypeLimitsFrame(AccountTypeLimitsFrame const& from);

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

};
}
