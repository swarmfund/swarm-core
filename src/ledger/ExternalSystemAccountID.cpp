// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "crypto/SecretKey.h"
#include "crypto/Hex.h"
#include "database/Database.h"
#include "LedgerDelta.h"
#include "ledger/LedgerManager.h"
#include "ledger/ExternalSystemAccountID.h"
#include "util/basen.h"
#include "util/types.h"
#include "lib/util/format.h"
#include "xdrpp/printer.h"
#include <algorithm>

using namespace soci;
using namespace std;

namespace stellar
{
using xdr::operator<;

ExternalSystemAccountIDFrame::
ExternalSystemAccountIDFrame() : EntryFrame(LedgerEntryType::
                                            EXTERNAL_SYSTEM_ACCOUNT_ID)
                               , mExternalSystemAccountID(mEntry.data.
                                                                 externalSystemAccountID())
{
}

ExternalSystemAccountIDFrame::ExternalSystemAccountIDFrame(
    LedgerEntry const& from)
    : EntryFrame(from)
    , mExternalSystemAccountID(mEntry.data.externalSystemAccountID())
{
}

ExternalSystemAccountIDFrame::ExternalSystemAccountIDFrame(
    ExternalSystemAccountIDFrame const&
    from) : ExternalSystemAccountIDFrame(from.mEntry)
{
}

ExternalSystemAccountIDFrame& ExternalSystemAccountIDFrame::operator=(
    ExternalSystemAccountIDFrame const& other)
{
    if (&other != this)
    {
        mExternalSystemAccountID = other.mExternalSystemAccountID;
        mKey = other.mKey;
        mKeyCalculated = other.mKeyCalculated;
    }
    return *this;
}

ExternalSystemAccountIDFrame::pointer ExternalSystemAccountIDFrame::createNew(
    AccountID const accountID, int32 const externalSystemType,
    string const data)
{
    LedgerEntry le;
    le.data.type(LedgerEntryType::EXTERNAL_SYSTEM_ACCOUNT_ID);
    auto& externalSystemAccountID = le.data.externalSystemAccountID();
    externalSystemAccountID.accountID = accountID;
    externalSystemAccountID.externalSystemType = externalSystemType;
    externalSystemAccountID.data = data;
    return std::make_shared<ExternalSystemAccountIDFrame>(le);
}

bool ExternalSystemAccountIDFrame::isValid(ExternalSystemAccountID const& oe)
{
    return !oe.data.empty();
}

bool ExternalSystemAccountIDFrame::isValid() const
{
    return isValid(mExternalSystemAccountID);
}

}

