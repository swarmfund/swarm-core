
#pragma once

#include "transactions/OperationFrame.h"

#include <limits>
#include <tuple>
#include <unordered_map>
#include <vector>

static const uint64_t priority_max =
    static_cast<uint64_t>(std::numeric_limits<uint64_t>::max());
static const constexpr uint64_t priority_space = (priority_max - 3 * 1024) / 2;

#define PRIORITY_USER_MIN 1024
#define PRIORITY_USER_MAX (priority_space + 1024)
#define PRIORITY_ADMIN_MIN (PRIORITY_USER_MAX + 1024 + priority_space)
#define PRIORITY_ADMIN_MAX (priority_limits.max() - 1024)

namespace stellar
{

class SetIdentityPolicyOpFrame : public OperationFrame
{
  private:
    std::vector<std::string> allowedResources = {"sale"};
    std::unordered_map<std::string, std::vector<std::string>>
        allowedResourceProperties = {{allowedResources[0], {"id"}}};

  public:
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
    SetIdentityPolicyResult&
    innerResult()
    {
        return mResult.tr().setIdentityPolicyResult();
    }

    SetIdentityPolicyOp const& mSetIdentityPolicy;

    bool trySetIdentityPolicy(Database& db, LedgerDelta& delta);

    bool checkResourceValue();
    std::tuple<std::string, std::string, std::string>
    getResourceFields();
};

} // namespace stellar
