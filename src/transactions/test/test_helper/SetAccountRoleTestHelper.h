#pragma once

#include "TxHelper.h"

namespace stellar
{
namespace txtest
{
class SetAccountRoleTestHelper : TxHelper
{
  public:
    explicit SetAccountRoleTestHelper(TestManager::pointer testManager);

    SetAccountRoleOp createCreationOpInput(const std::string& name);

    SetAccountRoleOp createDeletionOpInput(uint64_t accountRoleID);

    TransactionFramePtr createAccountRoleTx(Account& source,
                                            const SetAccountRoleOp& op);

    SetAccountRoleResult
    applySetAccountRole(Account& source, const SetAccountRoleOp& op,
                        SetAccountRoleResultCode expectedResultCode =
                            SetAccountRoleResultCode::SUCCESS);
};
} // namespace txtest
} // namespace stellar