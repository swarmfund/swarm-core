#pragma once

#include "ledger/ExternalSystemAccountIDPoolEntryHelper.h"
#include "ledger/ExternalSystemAccountIDPoolEntry.h"
#include "util/NonCopyable.h"

namespace soci
{
class session;
}

namespace stellar
{
class StatementContext;
class LedgerManager;
class StorageHelper;

class ExternalSystemAccountIDPoolEntryHelperImpl : public ExternalSystemAccountIDPoolEntryHelper, NonCopyable
{
  public:
    explicit ExternalSystemAccountIDPoolEntryHelperImpl(StorageHelper& storageHelper);

  private:
    void dropAll() override;
    void fixTypes() override;
    void parentToNumeric() override;
    void storeAdd(LedgerEntry const& entry) override;
    void storeChange(LedgerEntry const& entry) override;
    void storeDelete(LedgerKey const& key) override;
    bool exists(LedgerKey const& key) override;
    LedgerKey getLedgerKey(LedgerEntry const& from) override;
    EntryFrame::pointer storeLoad(LedgerKey const& key) override;
    EntryFrame::pointer fromXDR(LedgerEntry const& from) override;
    uint64_t countObjects() override;

    bool exists(uint64_t poolEntryID) override;
    bool existsForAccount(int32 externalSystemType,
                          AccountID accountID) override;

    ExternalSystemAccountIDPoolEntryFrame::pointer
    load(uint64_t poolEntryID) override;

    ExternalSystemAccountIDPoolEntryFrame::pointer
    load(int32 type, std::string data) override;
    ExternalSystemAccountIDPoolEntryFrame::pointer
    load(int32 externalSystemType, AccountID accountID) override;

    ExternalSystemAccountIDPoolEntryFrame::pointer
    loadAvailablePoolEntry(LedgerManager& ledgerManager,
                           int32 externalSystemType) override;

    std::vector<ExternalSystemAccountIDPoolEntryFrame::pointer>
    loadPool() override;

    Database& getDatabase() override;

  private:
    static const char* select;

    void storeUpdateHelper(bool insert, LedgerEntry const& entry);
    void load(StatementContext& prep,
              std::function<void(LedgerEntry const&)> processor);

    StorageHelper& mStorageHelper;
};
}
