#pragma once

#include "TxHelper.h"

namespace stellar
{
namespace txtest
{

class SetAccountRolePolicyTestHelper : TxHelper
{
  public:
    explicit SetAccountRolePolicyTestHelper(TestManager::pointer testManager);

    TransactionFramePtr
    createSetAccountRolePolicyTx(Account& source,
                                 AccountRolePolicyEntry policyEntry,
                                 ManageAccountRolePolicyOpAction action);

    void
    applySetIdentityPolicyTx(Account& source,
                             AccountRolePolicyEntry& policyEntry, ManageAccountRolePolicyOpAction action,
                             ManageAccountRolePolicyResultCode expectedResult =
                                 ManageAccountRolePolicyResultCode::SUCCESS);

    AccountRolePolicyEntry createAccountRolePolicyEntry(
        uint64_t id, AccountID owner, PolicyDetails* details = nullptr,
        AccountRolePolicyEffect effect = AccountRolePolicyEffect::ALLOW);
};

} // namespace txtest
} // namespace stellar