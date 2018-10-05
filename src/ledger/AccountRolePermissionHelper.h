#pragma once

#include "ledger/EntryHelper.h"
#include "ledger/AccountFrame.h"

namespace soci
{
class session;
}

namespace stellar
{
class StorageHelper;

class AccountRolePermissionHelper : public EntryHelper
{
  public:
    virtual bool
    hasPermission(const AccountFrame::pointer initiatorAccountFrame,
                  const OperationType opType) = 0;
};

} // namespace stellar