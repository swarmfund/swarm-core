#pragma once

// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "overlay/StellarXDR.h"
#include "bucket/LedgerCmp.h"
#include "util/NonCopyable.h"
#include "util/Logging.h"
#include "crypto/SecretKey.h"

/*
Frame
Parent of AccountFrame, CoinstEmissionRequestFrame etc

These just hold the xdr LedgerEntry objects and have some associated functions
*/

namespace stellar
{
class Database;
class LedgerDelta;

class EntryFrame : public NonMovableOrCopyable
{
  protected:
    mutable bool mKeyCalculated;
    mutable LedgerKey mKey;

  public:
    typedef std::shared_ptr<EntryFrame> pointer;

    LedgerEntry mEntry;

    EntryFrame(LedgerEntryType type);
    EntryFrame(LedgerEntry const& from);

	void clearCached()
	{
		mKeyCalculated = false;
	}

    // helpers to get/set the last modified field
    uint32 getLastModified() const;
    uint32& getLastModified();
    void touch(uint32 ledgerSeq);

    // touch the entry if the delta is tracking a ledger header with
    // a sequence that is not 0 (0 is used when importing buckets)
    void touch(LedgerDelta const& delta);

    virtual EntryFrame::pointer copy() const = 0;

    virtual LedgerKey const& getKey() const;
};

// static helper for getting a LedgerKey from a LedgerEntry.
LedgerKey LedgerEntryKey(LedgerEntry const& e);
}
