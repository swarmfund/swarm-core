
#pragma once

#include "ledger/AccountRolePolicyHelper.h"
#include "transactions/PolicyDetails.h"
#include "xdr/Stellar-types.h"

#include <array>
#include <regex>

namespace stellar
{

class IdentityPolicyChecker
{
  public:
    enum class FindResult
    {
        NOT_FOUND,
        ALLOW,
        DENY
    };
    IdentityPolicyChecker() = delete;

    static bool isPolicyAllowed(const AccountFrame::pointer initiatorAccountFrame,
                                const PolicyDetails& policyDetails,
                                Database& db, LedgerDelta* delta = nullptr);
    static FindResult findPolicy(uint32 accountRole,
                                 const PolicyDetails& policyDetails,
                                 Database& db);
};

} // namespace stellar