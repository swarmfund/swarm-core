#pragma once

#include "ledger/AccountRoleFrame.h"
#include "ledger/EntryHelper.h"
#include "ledger/LedgerManager.h"
#include "ledger/StorageHelper.h"

namespace soci
{
class session;
}

namespace stellar
{
class Application;
class StatementContext;

class AccountRoleHelper : public EntryHelper
{
  public:
    AccountRoleHelper(StorageHelper& storageHelper);

    void dropAll() override;
    void storeAdd(LedgerEntry const& entry) override;
    void storeChange(LedgerEntry const& entry) override;
    void storeDelete(LedgerKey const& key) override;
    bool exists(LedgerKey const& key) override;
    LedgerKey getLedgerKey(LedgerEntry const& from) override;
    EntryFrame::pointer fromXDR(LedgerEntry const& from) override;
    EntryFrame::pointer storeLoad(LedgerKey const& ledgerKey) override;
    uint64_t countObjects() override;

    Database& getDatabase() override;

  private:
    void storeUpdate(LedgerEntry const &entry, bool insert);
    StorageHelper& mStorageHelper;
};
} // namespace stellar