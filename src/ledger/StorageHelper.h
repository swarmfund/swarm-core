#pragma once

#include <memory>

namespace stellar
{
class Database;
class LedgerDelta;
class KeyValueHelper;
class ExternalSystemAccountIDHelper;
class ExternalSystemAccountIDPoolEntryHelper;

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
    virtual void release() = 0;

    std::unique_ptr<StorageHelper> startNestedTransaction = 0;

    virtual KeyValueHelper& getKeyValueHelper() = 0;
    virtual ExternalSystemAccountIDHelper&
    getExternalSystemAccountIDHelper() = 0;
    virtual ExternalSystemAccountIDPoolEntryHelper&
    getExternalSystemAccountIDPoolEntryHelper() = 0;
};
} // namespace stellar