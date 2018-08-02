#pragma once

#include "ledger/ExternalSystemAccountIDHelper.h"
#include "ledger/ExternalSystemAccountIDPoolEntryHelper.h"
#include "ledger/KeyValueHelper.h"
#include "ledger/StorageHelper.h"
#include <memory>

namespace stellar
{
class StorageHelperImpl : public StorageHelper
{
  public:
    StorageHelperImpl(Database& db, LedgerDelta& ledgerDelta);

  private:
    virtual Database& getDatabase();
    virtual const Database& getDatabase() const;
    virtual LedgerDelta& getLedgerDelta();
    virtual const LedgerDelta& getLedgerDelta() const;

    virtual void commit();
    virtual void rollback();

    virtual KeyValueHelper& getKeyValueHelper();
    virtual ExternalSystemAccountIDHelper& getExternalSystemAccountIDHelper();
    virtual ExternalSystemAccountIDPoolEntryHelper&
    getExternalSystemAccountIDPoolEntryHelper();

    Database& mDatabase;
    LedgerDelta& mLedgerDelta;

    std::unique_ptr<KeyValueHelper> mKeyValueHelper;
    std::unique_ptr<ExternalSystemAccountIDHelper>
        mExternalSystemAccountIDHelper;
    std::unique_ptr<ExternalSystemAccountIDPoolEntryHelper>
        mExternalSystemAccountIDPoolEntryHelper;
};
}