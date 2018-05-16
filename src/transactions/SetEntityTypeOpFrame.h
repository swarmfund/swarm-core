
#pragma once

#include "transactions/OperationFrame.h"

namespace stellar
{

class SetEntityTypeOpFrame : public OperationFrame
{
  public:
    SetEntityTypeOpFrame(Operation const& op, OperationResult& res,
                         TransactionFrame& parentTx);

    std::unordered_map<AccountID, CounterpartyDetails>
    getCounterpartyDetails(Database& db, LedgerDelta* delta) const override;
    SourceDetails
    getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails>
                                counterpartiesDetails,
                            int32_t ledgerVersion) const override;

    static SetEntityTypeResultCode
    getInnerCode(OperationResult const& res)
    {
        return res.tr().setEntityTypeResult().code();
    }

    std::string
    getInnerResultCodeAsStr() override
    {
        return xdr::xdr_traits<SetEntityTypeResultCode>::enum_name(
            innerResult().code());
    }

  protected:
    bool doApply(Application& app, LedgerDelta& delta,
                 LedgerManager& ledgerManager) override;

    bool doCheckValid(Application& app) override;

  private:
    SetEntityTypeResult&
    innerResult()
    {
        return mResult.tr().setEntityTypeResult();
    }

    SetEntityTypeOp const& mSetEntityType;

    bool trySetEntityType(Database& db, LedgerDelta& delta);
};

} // namespace stellar
