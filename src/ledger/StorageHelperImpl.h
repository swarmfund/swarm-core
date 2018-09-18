#pragma once

#include "ledger/StorageHelper.h"
#include "BalanceHelperLegacy.h"
#include <memory>

namespace soci
{
class transaction;
}

namespace stellar
{

class KeyValueHelper;
class BalanceHelper;
class AssetHelper;
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

    virtual KeyValueHelper& getKeyValueHelper();
    virtual BalanceHelper& getBalanceHelper();
    virtual AssetHelper& getAssetHelper();
    virtual ExternalSystemAccountIDHelper& getExternalSystemAccountIDHelper();
    virtual ExternalSystemAccountIDPoolEntryHelper&
    getExternalSystemAccountIDPoolEntryHelper();

    Database& mDatabase;
    LedgerDelta& mLedgerDelta;
    bool mIsReleased = false;
    std::unique_ptr<LedgerDelta> mNestedDelta;
    std::unique_ptr<soci::transaction> mTransaction;

    std::unique_ptr<KeyValueHelper> mKeyValueHelper;
    std::unique_ptr<BalanceHelper> mBalanceHelper;
    std::unique_ptr<AssetHelper> mAssetHelper;
    std::unique_ptr<ExternalSystemAccountIDHelper>
        mExternalSystemAccountIDHelper;
    std::unique_ptr<ExternalSystemAccountIDPoolEntryHelper>
        mExternalSystemAccountIDPoolEntryHelper;
};
} // namespace stellar