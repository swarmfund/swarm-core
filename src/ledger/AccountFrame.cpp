// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "AccountFrame.h"
#include "LedgerDelta.h"
#include "database/Database.h"
#include "ledger/AccountTypeLimitsFrame.h"
#include "lib/util/format.h"
#include "util/basen.h"
#include "util/types.h"

using namespace soci;
using namespace std;

namespace stellar
{
using xdr::operator<;

AccountFrame::AccountFrame()
    : EntryFrame(LedgerEntryType::ACCOUNT), mAccountEntry(mEntry.data.account())
{
    mAccountEntry.thresholds[0] = 1; // by default, master key's weight is 1
    mUpdateSigners = true;
}

AccountFrame::AccountFrame(LedgerEntry const& from)
    : EntryFrame(from), mAccountEntry(mEntry.data.account())
{
    // we cannot make any assumption on mUpdateSigners:
    // it's possible we're constructing an account with no signers
    // but that the database's state had a previous version with signers
    mUpdateSigners = true;
    mAccountEntry.limits = nullptr;
}

AccountFrame::AccountFrame(AccountFrame const& from) : AccountFrame(from.mEntry)
{
}

AccountFrame::AccountFrame(AccountID const& id) : AccountFrame()
{
    mAccountEntry.accountID = id;
}

AccountFrame::pointer
AccountFrame::makeAuthOnlyAccount(AccountID const& id)
{
    AccountFrame::pointer ret = make_shared<AccountFrame>(id);
    return ret;
}

bool
AccountFrame::signerCompare(Signer const& s1, Signer const& s2)
{
    return s1.pubKey < s2.pubKey;
}

void
AccountFrame::normalize()
{
    std::sort(mAccountEntry.signers.begin(), mAccountEntry.signers.end(),
              &AccountFrame::signerCompare);
}

bool
AccountFrame::isValid()
{
    auto const& a = mAccountEntry;
    return std::is_sorted(a.signers.begin(), a.signers.end(),
                          &AccountFrame::signerCompare);
}

bool
AccountFrame::isBlocked() const
{
    return mAccountEntry.blockReasons > 0;
}

void
AccountFrame::setBlockReasons(uint32 reasonsToAdd, uint32 reasonsToRemove)
{
    mAccountEntry.blockReasons |= reasonsToAdd;
    mAccountEntry.blockReasons &= ~reasonsToRemove;
}

AccountID const&
AccountFrame::getID() const
{
    return (mAccountEntry.accountID);
}

uint32_t
AccountFrame::getMasterWeight() const
{
    return mAccountEntry
        .thresholds[static_cast<int32_t>(ThresholdIndexes::MASTER_WEIGHT)];
}

uint32_t
AccountFrame::getHighThreshold() const
{
    return mAccountEntry
        .thresholds[static_cast<int32_t>(ThresholdIndexes::HIGH)];
}

uint32_t
AccountFrame::getMediumThreshold() const
{
    return mAccountEntry
        .thresholds[static_cast<int32_t>(ThresholdIndexes::MED)];
}

uint32_t
AccountFrame::getLowThreshold() const
{
    return mAccountEntry
        .thresholds[static_cast<int32_t>(ThresholdIndexes::LOW)];
}

uint32
AccountFrame::getKYCLevel() const
{
    if (mAccountEntry.ext.v() == LedgerVersion::USE_KYC_LEVEL)
    {
        return mAccountEntry.ext.kycLevel();
    }
    else if (mAccountEntry.ext.v() ==
             LedgerVersion::REPLACE_ACCOUNT_TYPES_WITH_POLICIES)
    {
        return mAccountEntry.ext.accountEntryExt().kycLevel;
    }
    else
    {
        return 0;
    }
}
void
AccountFrame::setKYCLevel(uint32 kycLevel)
{
    if (mAccountEntry.ext.v() == LedgerVersion::USE_KYC_LEVEL)
    {
        mAccountEntry.ext.kycLevel() = kycLevel;
    }
    else if (mAccountEntry.ext.v() ==
             LedgerVersion::REPLACE_ACCOUNT_TYPES_WITH_POLICIES)
    {
        mAccountEntry.ext.accountEntryExt().kycLevel = kycLevel;
    }
    else if (kycLevel != 0)
    {
        throw std::runtime_error("Could not read KYC Level");
    }
}
xdr::pointer<uint64>
AccountFrame::getAccountRole() const
{
    if (mAccountEntry.ext.v() ==
        LedgerVersion::REPLACE_ACCOUNT_TYPES_WITH_POLICIES)
    {
        return mAccountEntry.ext.accountEntryExt().accountRole;
    }
    else
    {
        return xdr::pointer<uint64>();
    }
}
void
AccountFrame::setAccountRole(xdr::pointer<uint64> accountRole)
{
    if (mAccountEntry.ext.v() ==
        LedgerVersion::REPLACE_ACCOUNT_TYPES_WITH_POLICIES)
    {
        mAccountEntry.ext.accountEntryExt().accountRole = accountRole;
    }
    else if (accountRole)
    {
        throw std::runtime_error("Could not set account role");
    }
}

} // namespace stellar
