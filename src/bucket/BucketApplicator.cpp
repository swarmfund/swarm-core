// Copyright 2015 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "bucket/Bucket.h"
#include "bucket/BucketApplicator.h"
#include "ledger/LedgerDeltaImpl.h"
#include "ledger/EntryHelperLegacy.h"
#include "util/Logging.h"

namespace stellar
{

BucketApplicator::BucketApplicator(Database& db,
                                   std::shared_ptr<const Bucket> bucket)
    : mDb(db), mBucket(bucket)
{
    if (!bucket->getFilename().empty())
    {
        mIn.open(bucket->getFilename());
    }
}

BucketApplicator::operator bool() const
{
    return (bool)mIn;
}

void
BucketApplicator::advance()
{
    soci::transaction sqlTx(mDb.getSession());
    BucketEntry entry;
    while (mIn && mIn.readOne(entry))
    {
        LedgerHeader lh;
        LedgerDeltaImpl delta(lh, mDb, false);
        if (entry.type() == BucketEntryType::LIVEENTRY)
        {
            EntryFrame::pointer ep = EntryHelperProvider::fromXDREntry(entry.liveEntry());
            EntryHelperProvider::storeAddOrChangeEntry(delta, mDb, ep->mEntry);
        }
        else
        {
			EntryHelperProvider::storeDeleteEntry(delta, mDb, entry.deadEntry());
        }
        // No-op, just to avoid needless rollback.
        static_cast<LedgerDelta&>(delta).commit();
        if ((++mSize & 0xff) == 0xff)
        {
            break;
        }
    }
    sqlTx.commit();
    mDb.clearPreparedStatementCache();

    if (!mIn || (mSize & 0xfff) == 0xfff)
    {
        CLOG(INFO, "Bucket") << "Bucket-apply: committed " << mSize
                             << " entries";
    }
}
}
