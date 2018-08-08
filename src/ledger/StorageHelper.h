#pragma once

#include <memory>

namespace stellar
{
class Database;
class LedgerDelta;
class KeyValueHelperLegacy;
class ExternalSystemAccountIDHelperLegacy;
class ExternalSystemAccountIDPoolEntryHelperLegacy;

class StorageHelper
{
  public:
    virtual ~StorageHelper()
    {
    }

    virtual Database& getDatabase() = 0;
    virtual const Database& getDatabase() const = 0;
    virtual LedgerDelta& getLedgerDelta() = 0;
    virtual const LedgerDelta& getLedgerDelta() const = 0;

    virtual void commit() = 0;
    virtual void rollback() = 0;

    std::unique_ptr<StorageHelper> startNestedTransaction = 0;

    virtual KeyValueHelperLegacy& getKeyValueHelper() = 0;
    virtual ExternalSystemAccountIDHelperLegacy&
    getExternalSystemAccountIDHelper() = 0;
    virtual ExternalSystemAccountIDPoolEntryHelperLegacy&
    getExternalSystemAccountIDPoolEntryHelper() = 0;
};
} // namespace stellar