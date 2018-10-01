#pragma once

#include "TxHelper.h"

namespace stellar
{
namespace txtest
{

class ManageAccountRolePermissionTestHelper : TxHelper
{
  public:
    explicit ManageAccountRolePermissionTestHelper(
        TestManager::pointer testManager);

    TransactionFramePtr createSetAccountRolePermissionTx(
        Account& source, AccountRolePermissionEntry permissionEntry,
        ManageAccountRolePermissionOpAction action);

    void applySetIdentityPermissionTx(
        Account& source, AccountRolePermissionEntry& permissionEntry,
        ManageAccountRolePermissionOpAction action,
        ManageAccountRolePermissionResultCode expectedResult =
            ManageAccountRolePermissionResultCode::SUCCESS);

    AccountRolePermissionEntry
    createAccountRolePermissionEntry(uint64_t id, OperationType operationType);
};

} // namespace txtest
} // namespace stellar