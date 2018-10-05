#pragma once

#include "AccountRolePermissionFrame.h"
#include "ledger/AccountRolePermissionHelper.h"
#include "ledger/StorageHelper.h"
#include "xdr/Stellar-types.h"

namespace stellar
{
class LedgerManager;

class AccountRolePermissionHelperImpl : public AccountRolePermissionHelper
{
  public:
    explicit AccountRolePermissionHelperImpl(StorageHelper& storageHelper);

  private:
    void dropAll() override;
    void storeAdd(LedgerEntry const& entry) override;
    void storeChange(LedgerEntry const& entry) override;
    void storeDelete(LedgerKey const& key) override;
    bool exists(LedgerKey const& key) override;
    LedgerKey getLedgerKey(LedgerEntry const& from) override;
    EntryFrame::pointer fromXDR(LedgerEntry const& from) override;
    EntryFrame::pointer storeLoad(LedgerKey const& ledgerKey) override;
    uint64_t countObjects() override;
    bool hasPermission(const AccountFrame::pointer initiatorAccountFrame,
                       const OperationType opType) override;

    Database& getDatabase() override;

    bool checkPermission(uint32 accountRole, const OperationType opType);
    void storeUpdate(LedgerEntry const& entry, bool insert);
    Database& mDb;
    LedgerDelta* mLedgerDelta{nullptr};
};

} // namespace stellar
