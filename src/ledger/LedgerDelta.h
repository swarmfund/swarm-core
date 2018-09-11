#pragma once

#include <map>
#include <set>
#include "ledger/EntryFrame.h"

namespace stellar
{
class LedgerHeader;
class LedgerHeaderFrame;
class Application;

class LedgerDelta
{
  public:
    typedef std::map<LedgerKey, EntryFrame::pointer, LedgerEntryIdCmp>
        KeyEntryMap;
    virtual LedgerHeader& getHeader() = 0;
    virtual LedgerHeader const& getHeader() const = 0;
    virtual LedgerHeaderFrame& getHeaderFrame() = 0;

    // methods to register changes in the ledger entries
    virtual void addEntry(EntryFrame const& entry) = 0;
    virtual void deleteEntry(EntryFrame const& entry) = 0;
    virtual void deleteEntry(LedgerKey const& key) = 0;
    virtual void modEntry(EntryFrame const& entry) = 0;
    virtual void recordEntry(EntryFrame const& entry) = 0;

    virtual void mergeEntries(LedgerDelta& other) = 0;

    // commits this delta into outer delta
    virtual void commit() = 0;
    // aborts any changes pending, flush db cache entries
    virtual void rollback() = 0;

    virtual bool updateLastModified() const = 0;

    virtual void markMeters(Application& app) const = 0;

    virtual std::vector<LedgerEntry> getLiveEntries() const = 0;
    virtual std::vector<LedgerKey> getDeadEntries() const = 0;

    virtual LedgerEntryChanges getChanges() const = 0;
    virtual const LedgerEntryChanges& getAllChanges() const = 0;

    // performs sanity checks against the local state
    virtual void checkAgainstDatabase(Application& app) const = 0;

    virtual KeyEntryMap getState() const = 0;

    virtual bool isStateActive() const = 0;

    virtual Database& getDatabase() = 0;

    virtual const KeyEntryMap& getPreviousFrames() const = 0;
    virtual const std::set<LedgerKey, LedgerEntryIdCmp>& getDeletionFramesSet() const = 0;
    virtual const KeyEntryMap& getCreationFrames() const = 0;
    virtual const KeyEntryMap& getModificationFrames() const = 0;
};

} // namespace stellar
