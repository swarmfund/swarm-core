// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ledger/ReferenceFrame.h"
#include "database/Database.h"
#include "crypto/SecretKey.h"
#include "crypto/SHA.h"
#include "LedgerDelta.h"
#include "util/types.h"
#include "xdrpp/printer.h"

using namespace std;
using namespace soci;

namespace stellar
{

ReferenceFrame::ReferenceFrame() : EntryFrame(LedgerEntryType::REFERENCE_ENTRY), mReference(mEntry.data.reference())
{
}

ReferenceFrame::ReferenceFrame(LedgerEntry const& from)
    : EntryFrame(from), mReference(mEntry.data.reference())
{
}

ReferenceFrame::ReferenceFrame(ReferenceFrame const& from) : ReferenceFrame(from.mEntry)
{
}

ReferenceFrame& ReferenceFrame::operator=(ReferenceFrame const& other)
{
    if (&other != this)
    {
        mReference = other.mReference;
        mKey = other.mKey;
        mKeyCalculated = other.mKeyCalculated;
    }
    return *this;
}

ReferenceFrame::pointer ReferenceFrame::create(AccountID sender, stellar::string64 reference)
{
	LedgerEntry le;
	le.data.type(LedgerEntryType::REFERENCE_ENTRY);
	auto& referenceEntry = le.data.reference();
	referenceEntry.reference = reference;
	referenceEntry.sender = sender;
	return std::make_shared<ReferenceFrame>(le);
}

bool
ReferenceFrame::isValid(ReferenceEntry const& oe)
{
    return true;
}

bool
ReferenceFrame::isValid() const
{
    return isValid(mReference);
}
}
