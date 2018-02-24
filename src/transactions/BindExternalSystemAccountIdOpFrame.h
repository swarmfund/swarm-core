#pragma once

#include "transactions/OperationFrame.h"

namespace stellar
{

class BindExternalSystemAccountIdOpFrame : public OperationFrame
{
    BindExternalSystemAccountIdResult&
    innerResult()
    {
        return mResult.tr().bindExternalSystemAccountIdResult();
    }
    BindExternalSystemAccountIdOp const& mBindExternalSystemAccountId;

    std::unordered_map<AccountID, CounterpartyDetails> getCounterpartyDetails(Database& db, LedgerDelta* delta) const override;
    SourceDetails getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
                                          int32_t ledgerVersion) const override;
public:
    BindExternalSystemAccountIdOpFrame(Operation const& op, OperationResult& res,
                                       TransactionFrame& parentTx);

    bool doApply(Application& app, LedgerDelta& delta, LedgerManager& ledgerManager) override;
    bool doCheckValid(Application& app) override;

    static BindExternalSystemAccountIdResultCode
    getInnerCode(OperationResult const& res)
    {
        return res.tr().bindExternalSystemAccountIdResult().code();
    }
};
}