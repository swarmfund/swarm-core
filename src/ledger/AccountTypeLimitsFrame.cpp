// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ledger/AccountTypeLimitsFrame.h"
#include "database/Database.h"
#include "crypto/SecretKey.h"
#include "crypto/SHA.h"
#include "LedgerDelta.h"
#include "util/types.h"

using namespace std;
using namespace soci;

namespace stellar
{

AccountTypeLimitsFrame::AccountTypeLimitsFrame() : EntryFrame(LedgerEntryType::ACCOUNT_TYPE_LIMITS), mAccountTypeLimits(mEntry.data.accountTypeLimits())
{
}

AccountTypeLimitsFrame::AccountTypeLimitsFrame(LedgerEntry const& from)
    : EntryFrame(from), mAccountTypeLimits(mEntry.data.accountTypeLimits())
{
}

AccountTypeLimitsFrame::AccountTypeLimitsFrame(AccountTypeLimitsFrame const& from) : AccountTypeLimitsFrame(from.mEntry)
{
}

AccountTypeLimitsFrame& AccountTypeLimitsFrame::operator=(AccountTypeLimitsFrame const& other)
{
    if (&other != this)
    {
        mAccountTypeLimits = other.mAccountTypeLimits;
        mKey = other.mKey;
        mKeyCalculated = other.mKeyCalculated;
    }
    return *this;
}

bool
AccountTypeLimitsFrame::isValid(AccountTypeLimitsEntry const& oe)
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
AccountTypeLimitsFrame::isValid() const
{
    return isValid(mAccountTypeLimits);
}

}
