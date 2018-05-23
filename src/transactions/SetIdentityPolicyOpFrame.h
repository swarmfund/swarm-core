
#pragma once

#include "transactions/OperationFrame.h"

#include <limits>
#include <tuple>
#include <unordered_map>
#include <vector>

static const uint64_t priority_max =
    static_cast<uint64_t>(std::numeric_limits<uint64_t>::max());
static const constexpr uint64_t reserved_priority_space =
    (priority_max / 3) - 2048 - 2;

#define PRIORITY_USER_MIN reserved_priority_space + 1
#define PRIORITY_USER_MAX (PRIORITY_USER_MIN + 1024)
#define PRIORITY_ADMIN_MIN (PRIORITY_USER_MAX + reserved_priority_space + 1)
#define PRIORITY_ADMIN_MAX (PRIORITY_ADMIN_MIN + 1024)

namespace stellar
{

class SetIdentityPolicyOpFrame : public OperationFrame
{
  public:
    static const uint64_t policiesAmountLimit;

    SetIdentityPolicyOpFrame(Operation const& op, OperationResult& res,
                             TransactionFrame& parentTx);

    bool doApply(Application& app, LedgerDelta& delta,
                 LedgerManager& ledgerManager) override;
    bool doCheckValid(Application& app) override;

    std::unordered_map<AccountID, CounterpartyDetails>
    getCounterpartyDetails(Database& db, LedgerDelta* delta) const override;
    SourceDetails
    getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails>
                                counterpartiesDetails,
                            int32_t ledgerVersion) const override;

    static SetIdentityPolicyResultCode
    getInnerCode(OperationResult const& res)
    {
        return res.tr().setIdentityPolicyResult().code();
    }

    std::string
    getInnerResultCodeAsStr() override
    {
        return xdr::xdr_traits<SetIdentityPolicyResultCode>::enum_name(
            innerResult().code());
    }

  private:
    SetIdentityPolicyOp const& mSetIdentityPolicy;

    SetIdentityPolicyResult&
    innerResult()
    {
        return mResult.tr().setIdentityPolicyResult();
    }

    bool trySetIdentityPolicy(Database& db, LedgerDelta& delta);
};

} // namespace stellar
