
#pragma once

#include "ledger/AccountRolePolicyHelper.h"
#include "xdr/Stellar-types.h"
#include "transactions/PolicyDetails.h"

#include <array>
#include <regex>

namespace stellar
{

class IdentityPolicyChecker
{
  public:
    IdentityPolicyChecker() = delete;

    static bool doCheckPolicies(const AccountID &masterID,
                                const PolicyDetails &policyDetails,
                                Database &db,
                                LedgerDelta* delta = nullptr);
};

} // namespace stellar