#pragma once

#include "transactions/OperationFrame.h"

namespace stellar
{

class ManageExternalSystemProviderOpFrame : public OperationFrame
{
    ManageExternalSystemIdProviderResult&
    innerResult()
    {
        return mResult.tr().manageExternalSystemIdProviderResult();
    }
    ManageExternalSystemIdProviderOp const& mManageExternalSystemIdProviderOp;

    std::unordered_map<AccountID, CounterpartyDetails> getCounterpartyDetails(Database& db,
                                                                              LedgerDelta* delta) const override;
    SourceDetails getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
                                          int32_t ledgerVersion) const override;
public:
    ManageExternalSystemProviderOpFrame(Operation const& op, OperationResult& res,
                                        TransactionFrame& parentTx);

    bool doApply(Application& app, LedgerDelta& delta, LedgerManager& ledgerManager) override;
    bool doCheckValid(Application& app) override;

    static ManageExternalSystemIdProviderResultCode getInnerCode(OperationResult const& res)
    {
        return res.tr().manageExternalSystemIdProviderResult().code();
    }
};
}
