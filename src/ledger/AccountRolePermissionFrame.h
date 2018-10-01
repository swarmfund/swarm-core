#pragma once

#include "ledger/AccountFrame.h"
#include "ledger/EntryFrame.h"
#include "util/types.h"

#include <functional>
#include <stdio.h>
#include <unordered_map>

namespace stellar
{
class AccountRolePermissionFrame : public EntryFrame
{
    AccountRolePermissionEntry& mAccountRolePermissionEntry;
    AccountRolePermissionFrame(AccountRolePermissionFrame const& from);

  public:
    AccountRolePermissionFrame();
    explicit AccountRolePermissionFrame(LedgerEntry const& from);

    using pointer = std::shared_ptr<AccountRolePermissionFrame>;

    AccountRolePermissionEntry&
    getPermissionEntry() const
    {
        return mAccountRolePermissionEntry;
    }

    uint64_t
    getID() const
    {
        return mAccountRolePermissionEntry.permissionID;
    }

    uint64_t
    getRoleID() const
    {
        return mAccountRolePermissionEntry.accountRoleID;
    }

    OperationType
    getOperationType() const
    {
        return mAccountRolePermissionEntry.opType;
    }

    EntryFrame::pointer copy() const override;

    static void ensureValid(const AccountRolePermissionEntry& entry);
    void ensureValid() const;
};

} // namespace stellar
