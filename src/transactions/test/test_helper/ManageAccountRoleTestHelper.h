#pragma once

#include "TxHelper.h"

namespace stellar
{
namespace txtest
{
class ManageAccountRoleTestHelper : TxHelper
{
  public:
    explicit ManageAccountRoleTestHelper(TestManager::pointer testManager);

    ManageAccountRoleOp createCreationOpInput(const std::string& name);

    ManageAccountRoleOp createDeletionOpInput(uint64_t accountRoleID);

    TransactionFramePtr createAccountRoleTx(Account& source,
                                            const ManageAccountRoleOp& op);

    ManageAccountRoleResult
    applySetAccountRole(Account& source, const ManageAccountRoleOp& op,
                        ManageAccountRoleResultCode expectedResultCode =
                            ManageAccountRoleResultCode::SUCCESS);
};
} // namespace txtest
} // namespace stellar