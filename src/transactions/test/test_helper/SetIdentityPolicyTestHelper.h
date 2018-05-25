
#pragma once

#include "TxHelper.h"

namespace stellar
{
namespace txtest
{

class SetIdentityPolicyTestHelper : TxHelper {
public:
  explicit SetIdentityPolicyTestHelper(TestManager::pointer testManager);

    TransactionFramePtr createSetIdentityPolicyTx(Account &source,
                                                  IdentityPolicyEntry policyEntry,
                                                  bool isDelete);

    void applySetIdentityPolicyTx(Account &source,
                                  IdentityPolicyEntry &policyEntry,
                                  bool isDelete,
                                  SetIdentityPolicyResultCode expectedResult = SetIdentityPolicyResultCode::SUCCESS);

    IdentityPolicyEntry createIdentityPolicyEntry(uint64_t id, AccountID owner,
                                                  SetIdentityPolicyData *data = nullptr);
};


} // namespace txtest
} // namespace stellar