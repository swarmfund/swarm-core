#pragma once

#include "transactions/OperationFrame.h"

#include <limits>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace stellar
{

class SetAccountRolePolicyOpFrame : public OperationFrame
{
  public:
    SetAccountRolePolicyOpFrame(Operation const& op, OperationResult& res,
                                TransactionFrame& parentTx);

    bool doApply(Application& app, StorageHelper& storageHelper,
                 LedgerManager& ledgerManager) override;
    bool doCheckValid(Application& app) override;

    std::unordered_map<AccountID, CounterpartyDetails>
    getCounterpartyDetails(Database& db, LedgerDelta* delta) const override;
    SourceDetails
    getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails>
                                counterpartiesDetails,
                            int32_t ledgerVersion) const override;

    static SetAccountRolePolicyResultCode
    getInnerCode(OperationResult const& res)
    {
        return res.tr().setAccountRolePolicyResult().code();
    }

    std::string
    getInnerResultCodeAsStr() override
    {
        return xdr::xdr_traits<SetAccountRolePolicyResultCode>::enum_name(
            innerResult().code());
    }

  private:
    SetAccountRolePolicyOp const& mSetAccountRolePolicy;

    SetAccountRolePolicyResult&
    innerResult()
    {
        return mResult.tr().setAccountRolePolicyResult();
    }

    bool createOrUpdatePolicy(Application &app, StorageHelper &storageHelper);
    bool deleteAccountPolicy(Application &app, StorageHelper &storageHelper);

    static bool isDeleteOp(const SetAccountRolePolicyOp& accountRolePolicyOp);
};

} // namespace stellar
