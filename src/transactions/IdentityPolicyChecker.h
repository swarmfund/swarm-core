
#pragma once

#include "ledger/IdentityPolicyHelper.h"
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

  private:
    static bool checkPolicy(IdentityPolicyFrame::pointer policy,
                            uint64_t& lastPriority);
};

} // namespace stellar