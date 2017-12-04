// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ledger/AccountLimitsFrame.h"
#include "database/Database.h"
#include "crypto/SecretKey.h"
#include "crypto/SHA.h"
#include "LedgerDelta.h"
#include "util/types.h"

using namespace std;
using namespace soci;

namespace stellar
{

AccountLimitsFrame::AccountLimitsFrame() : EntryFrame(LedgerEntryType::ACCOUNT_TYPE_LIMITS), mAccountLimits(mEntry.data.accountLimits())
{
}

AccountLimitsFrame::AccountLimitsFrame(LedgerEntry const& from)
    : EntryFrame(from), mAccountLimits(mEntry.data.accountLimits())
{
}

AccountLimitsFrame::AccountLimitsFrame(AccountLimitsFrame const& from) : AccountLimitsFrame(from.mEntry)
{
}

AccountLimitsFrame& AccountLimitsFrame::operator=(AccountLimitsFrame const& other)
{
    if (&other != this)
    {
        mAccountLimits = other.mAccountLimits;
        mKey = other.mKey;
        mKeyCalculated = other.mKeyCalculated;
    }
    return *this;
}

bool
AccountLimitsFrame::isValid(AccountLimitsEntry const& oe)
{
    auto limits = oe.limits;
    if (limits.dailyOut > limits.weeklyOut)
        return false;
    if (limits.weeklyOut > limits.monthlyOut)
        return false;
    if (limits.monthlyOut > limits.annualOut)
        return false;
    return true;
}

bool
AccountLimitsFrame::isValid() const
{
    return isValid(mAccountLimits);
}

AccountLimitsFrame::pointer
AccountLimitsFrame::createNew(AccountID accountID, Limits limits)
{
    LedgerEntry le;
    le.data.type(LedgerEntryType::ACCOUNT_LIMITS);
    AccountLimitsEntry& entry = le.data.accountLimits();

    entry.accountID = accountID;
    entry.limits = limits;
    auto accountLimitsFrame = std::make_shared<AccountLimitsFrame>(le);
    return accountLimitsFrame;
}

}
