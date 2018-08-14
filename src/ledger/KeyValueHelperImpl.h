#pragma once

#include "ledger/KeyValueHelper.h"
#include "util/NonCopyable.h"

namespace soci
{
class session;
}

namespace stellar
{
class StorageHelper;

class KeyValueHelperImpl : public KeyValueHelper, NonCopyable
{

  public:
    explicit KeyValueHelperImpl(StorageHelper& storageHelper);

  private:
    void dropAll() override;

    void storeAdd(LedgerEntry const& entry) override;

    void storeChange(LedgerEntry const& entry) override;

    void storeDelete(LedgerKey const& key) override;

    bool exists(LedgerKey const& key) override;

    LedgerKey getLedgerKey(LedgerEntry const& from) override;

    EntryFrame::pointer storeLoad(LedgerKey const& key) override;

    EntryFrame::pointer fromXDR(LedgerEntry const& from) override;

    uint64_t countObjects() override;

    KeyValueEntryFrame::pointer loadKeyValue(string256 valueKey) override;

    void loadKeyValues(
        StatementContext& prep,
        std::function<void(LedgerEntry const&)> keyValueProcessor) override;

  private:
    void storeUpdateHelper(LedgerDelta& delta, Database& db, bool insert,
                           LedgerEntry const& entry);

    Database& getDatabase() override;

    StorageHelper& mStorageHelper;
};

} // namespace stellar
