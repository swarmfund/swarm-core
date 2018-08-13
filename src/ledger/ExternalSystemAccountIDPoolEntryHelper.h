#pragma once

#include "ledger/EntryHelper.h"
#include "ledger/ExternalSystemAccountIDPoolEntry.h"

namespace soci
{
class session;
}

namespace stellar
{
class StatementContext;
class LedgerManager;

class ExternalSystemAccountIDPoolEntryHelper : public EntryHelper
{
  public:
    virtual void fixTypes() = 0;
    virtual void parentToNumeric() = 0;

    virtual bool exists(uint64_t poolEntryID) = 0;
    virtual bool existsForAccount(int32 externalSystemType,
                          AccountID accountID) = 0;

    virtual ExternalSystemAccountIDPoolEntryFrame::pointer
    load(uint64_t poolEntryID) = 0;

    virtual ExternalSystemAccountIDPoolEntryFrame::pointer
    load(int32 type, std::string data) = 0;
    virtual ExternalSystemAccountIDPoolEntryFrame::pointer
    load(int32 externalSystemType, AccountID accountID) = 0;

    virtual ExternalSystemAccountIDPoolEntryFrame::pointer
    loadAvailablePoolEntry(LedgerManager& ledgerManager,
                           int32 externalSystemType) = 0;

    virtual std::vector<ExternalSystemAccountIDPoolEntryFrame::pointer>
    loadPool() = 0;
};
}
