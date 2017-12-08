// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ledger/InvoiceFrame.h"
#include "database/Database.h"
#include "crypto/SecretKey.h"
#include "crypto/SHA.h"
#include "LedgerDelta.h"
#include "util/types.h"

using namespace std;
using namespace soci;

namespace stellar
{

InvoiceFrame::InvoiceFrame() : EntryFrame(LedgerEntryType::INVOICE), mInvoice(mEntry.data.invoice())
{
}

InvoiceFrame::InvoiceFrame(LedgerEntry const& from)
    : EntryFrame(from), mInvoice(mEntry.data.invoice())
{
}

InvoiceFrame::InvoiceFrame(InvoiceFrame const& from) : InvoiceFrame(from.mEntry)
{
}

InvoiceFrame& InvoiceFrame::operator=(InvoiceFrame const& other)
{
    if (&other != this)
    {
        mInvoice = other.mInvoice;
        mKey = other.mKey;
        mKeyCalculated = other.mKeyCalculated;
    }
    return *this;
}

bool
InvoiceFrame::isValid(InvoiceEntry const& oe)
{
	return oe.amount > 0;
}

bool
InvoiceFrame::isValid() const
{
    return isValid(mInvoice);
}
}
