#pragma once

#include "ledger/ExternalSystemAccountIDHelperLegacy.h"
#include "ledger/ExternalSystemAccountIDPoolEntryHelperLegacy.h"
#include "ledger/KeyValueHelperLegacy.h"
#include "ledger/StorageHelper.h"
#include <memory>

namespace stellar
{
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

    virtual std::unique_ptr<StorageHelper> startNestedTransaction();

    virtual KeyValueHelperLegacy& getKeyValueHelper();
    virtual ExternalSystemAccountIDHelperLegacy& getExternalSystemAccountIDHelper();
    virtual ExternalSystemAccountIDPoolEntryHelperLegacy&
    getExternalSystemAccountIDPoolEntryHelper();

    Database& mDatabase;
    LedgerDelta& mLedgerDelta;
    std::unique_ptr<LedgerDelta> mNestedDelta;

    std::unique_ptr<soci::transaction> mTransaction;

    std::unique_ptr<KeyValueHelperLegacy> mKeyValueHelper;
    std::unique_ptr<ExternalSystemAccountIDHelperLegacy>
        mExternalSystemAccountIDHelper;
    std::unique_ptr<ExternalSystemAccountIDPoolEntryHelperLegacy>
        mExternalSystemAccountIDPoolEntryHelper;
};
}