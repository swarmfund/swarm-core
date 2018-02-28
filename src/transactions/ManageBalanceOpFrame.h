#pragma once

// Copyright 2015 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "transactions/OperationFrame.h"

namespace stellar
{

class ManageBalanceOpFrame : public OperationFrame
{
    ManageBalanceResult&
    innerResult()
    {
        return mResult.tr().manageBalanceResult();
    }
    ManageBalanceOp const& mManageBalance;

	std::unordered_map<AccountID, CounterpartyDetails> getCounterpartyDetails(Database& db, LedgerDelta* delta) const override;
	SourceDetails getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
                                              int32_t ledgerVersion) const override;
  public:
    
    ManageBalanceOpFrame(Operation const& op, OperationResult& res,
                         TransactionFrame& parentTx);

    bool doApply(Application& app, LedgerDelta& delta,
                 LedgerManager& ledgerManager) override;
    bool doCheckValid(Application& app) override;

    static ManageBalanceResultCode
    getInnerCode(OperationResult const& res)
    {
        return res.tr().manageBalanceResult().code();
    }

    std::string getInnerResultCodeAsStr() override
    {
        const auto result = getResult();
        const auto code = getInnerCode(result);
        return xdr::xdr_traits<ManageBalanceResultCode>::enum_name(code);
    }
};
}
