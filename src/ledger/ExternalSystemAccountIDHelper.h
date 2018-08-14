#pragma once

#include "ledger/EntryHelper.h"
#include "ledger/ExternalSystemAccountID.h"

namespace soci
{
class session;
}

namespace stellar
{
class StorageHelper;

class ExternalSystemAccountIDHelper : public EntryHelper
{
  public:
    virtual bool exists(AccountID accountID, int32 externalSystemType) = 0;

    virtual std::vector<ExternalSystemAccountIDFrame::pointer> loadAll() = 0;

    // loads external system account ID by accountID and externalSystemType. If
    // not found returns nullptr.
    virtual ExternalSystemAccountIDFrame::pointer
    load(const AccountID accountID, const int32 externalSystemType) = 0;
};

} // namespace stellar