#pragma once

#include "ledger/AccountRolePermissionHelper.h"
#include "xdr/Stellar-types.h"

namespace stellar
{

class IdentityPolicyChecker
{
  public:
    IdentityPolicyChecker() = delete;

    static bool
    isPolicyAllowed(const AccountFrame::pointer initiatorAccountFrame,
                    const OperationType opType, Database& db);
    static bool checkPolicy(uint32 accountRole, const OperationType opType,
                            Database& db);
};

} // namespace stellar