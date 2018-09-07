#pragma once

// Copyright 2015 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "bucket/LedgerCmp.h"
#include "ledger/LedgerDelta.h"
#include "ledger/LedgerHeaderFrameImpl.h"
#include "xdrpp/marshal.h"
#include <set>

namespace stellar
{
class Application;
class Database;

class LedgerDeltaImpl : public LedgerDelta
{
  private:
    LedgerDelta*
        mOuterDelta;       // set when this delta is nested inside another delta
    LedgerHeader* mHeader; // LedgerHeader to commit changes to

    // objects to keep track of changes
    // ledger header itself
    LedgerHeaderFrameImpl mCurrentHeader;
    LedgerHeader mPreviousHeaderValue;
    // ledger entries
    KeyEntryMap mNew;
    KeyEntryMap mMod;
    std::set<LedgerKey, LedgerEntryIdCmp> mDelete;

    // all created/changed ledger entries:
    LedgerEntryChanges mAllChanges;

    KeyEntryMap mPrevious;

    Database& mDb; // Used strictly for rollback of db entry cache.

    bool mUpdateLastModified;

    void checkState();
    void addEntry(EntryFrame::pointer entry);
    void deleteEntry(EntryFrame::pointer entry);
    void modEntry(EntryFrame::pointer entry);
    void recordEntry(EntryFrame::pointer entry);

    // merge "other" into current ledgerDelta
    void mergeEntries(LedgerDelta& other) override;

    // helper method that adds a meta entry to "changes"
    // with the previous value of an entry if needed
    void addCurrentMeta(LedgerEntryChanges& changes,
                        LedgerKey const& key) const;

  public:
    // keeps an internal reference to the outerDelta,
    // will apply changes to the outer scope on commit
    LedgerDeltaImpl(LedgerDelta& outerDelta);

    // keeps an internal reference to the outerDelta,
    // will apply changes to it on commit
    LedgerDeltaImpl(LedgerDeltaImpl& outerDelta);

    // keeps an internal reference to ledgerHeader,
    // will apply changes to ledgerHeader on commit,
    // will clear db entry cache on rollback.
    // updateLastModified: if true, revs the lastModified field
    LedgerDeltaImpl(LedgerHeader& ledgerHeader, Database& db,
                    bool updateLastModified = true);

    ~LedgerDeltaImpl();

  private:

    LedgerHeader& getHeader() override;
    LedgerHeader const& getHeader() const override;
    LedgerHeaderFrame& getHeaderFrame() override;

    // methods to register changes in the ledger entries
    void addEntry(EntryFrame const& entry) override;
    void deleteEntry(EntryFrame const& entry) override;
    void deleteEntry(LedgerKey const& key) override;
    void modEntry(EntryFrame const& entry) override;
    void recordEntry(EntryFrame const& entry) override;

    // commits this delta into outer delta
    void commit() override;
    // aborts any changes pending, flush db cache entries
    void rollback() override;

    bool updateLastModified() const override;

    void markMeters(Application& app) const override;

    std::vector<LedgerEntry> getLiveEntries() const override;
    std::vector<LedgerKey> getDeadEntries() const override;

    LedgerEntryChanges getChanges() const override;
    const LedgerEntryChanges& getAllChanges() const override;

    // performs sanity checks against the local state
    void checkAgainstDatabase(Application& app) const override;

    KeyEntryMap
    getState() const override
    {
        return mPrevious;
    }

    bool isStateActive() const;

    Database& getDatabase() override;

    const KeyEntryMap& getPreviousFrames() const override;
    const std::set<LedgerKey, LedgerEntryIdCmp>& getDeletionFramesSet() const override;
    const KeyEntryMap& getCreationFrames() const override;
    const KeyEntryMap& getModificationFrames() const override;
};
} // namespace stellar
