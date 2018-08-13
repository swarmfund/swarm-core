#pragma once

#include <memory>
#include "ledger/EntryHelperLegacy.h"

namespace stellar
{
class Database;
class LedgerEntry;
class LedgerKey;

class EntryHelper
{
  public:
    virtual void dropAll() = 0;

    virtual void storeAdd(LedgerEntry const& entry) = 0;
    virtual void storeChange(LedgerEntry const& entry) = 0;
    virtual void storeDelete(LedgerKey const& key) = 0;
    virtual bool exists(LedgerKey const& key) = 0;
    virtual LedgerKey getLedgerKey(LedgerEntry const& from) = 0;
    virtual EntryFrame::pointer fromXDR(LedgerEntry const& from) = 0;
    virtual EntryFrame::pointer storeLoad(LedgerKey const& ledgerKey) = 0;
    virtual uint64_t countObjects() = 0;

    virtual Database& getDatabase() = 0;

    virtual void flushCachedEntry(LedgerKey const& key);
    virtual bool cachedEntryExists(LedgerKey const& key);

  protected:
    virtual std::shared_ptr<LedgerEntry const> getCachedEntry(LedgerKey const& key);
    virtual void putCachedEntry(LedgerKey const& key,
                        std::shared_ptr<LedgerEntry const> p);
};

} // namespace stellar