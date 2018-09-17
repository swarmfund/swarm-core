
#pragma once

#include "AccountRolePolicyFrame.h"
#include "ledger/EntryHelper.h"
#include "ledger/StorageHelper.h"
#include "map"
#include "xdr/Stellar-types.h"
#include <functional>
#include <unordered_map>

namespace stellar
{
class LedgerManager;

class AccountRolePolicyHelper : public EntryHelper
{
  public:
    explicit AccountRolePolicyHelper(StorageHelper& storageHelper);
    explicit AccountRolePolicyHelper(Database& db);

    static void dropAll(Database& db);

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
    void storeUpdate(LedgerEntry const& entry, bool insert);
    Database& mDb;
    LedgerDelta* mLedgerDelta{nullptr};
};

} // namespace stellar
