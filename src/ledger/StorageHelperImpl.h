#pragma once

#include "ledger/StorageHelper.h"
#include "ledger/AccountRoleHelper.h"
#include "ledger/AccountRolePolicyHelper.h"
#include <memory>

namespace soci
{
class transaction;
}

namespace stellar
{

class KeyValueHelper;
class ExternalSystemAccountIDHelper;
class ExternalSystemAccountIDPoolEntryHelper;

class StorageHelperImpl : public StorageHelper
{
  public:
    StorageHelperImpl(Database& db, LedgerDelta& ledgerDelta);
    virtual ~StorageHelperImpl();

  private:
    virtual Database& getDatabase();
    virtual const Database& getDatabase() const;
    virtual LedgerDelta& getLedgerDelta();
    virtual const LedgerDelta& getLedgerDelta() const;

    virtual void commit();
    virtual void rollback();
    virtual void release();

    virtual std::unique_ptr<StorageHelper> startNestedTransaction();

    KeyValueHelper& getKeyValueHelper() override;
    ExternalSystemAccountIDHelper& getExternalSystemAccountIDHelper() override;
    ExternalSystemAccountIDPoolEntryHelper&
    getExternalSystemAccountIDPoolEntryHelper() override;
    AccountRoleHelper& getAccountRoleHelper() override;
    AccountRolePolicyHelper& getAccountRolePolicyHelper() override;

    Database& mDatabase;
    LedgerDelta& mLedgerDelta;
    bool mIsReleased = false;
    std::unique_ptr<LedgerDelta> mNestedDelta;
    std::unique_ptr<soci::transaction> mTransaction;

    std::unique_ptr<KeyValueHelper> mKeyValueHelper;
    std::unique_ptr<ExternalSystemAccountIDHelper>
        mExternalSystemAccountIDHelper;
    std::unique_ptr<ExternalSystemAccountIDPoolEntryHelper>
        mExternalSystemAccountIDPoolEntryHelper;
    std::unique_ptr<AccountRoleHelper> mAccountRoleHelper;
    std::unique_ptr<AccountRolePolicyHelper> mAccountRolePolicyHelper;
};
} // namespace stellar