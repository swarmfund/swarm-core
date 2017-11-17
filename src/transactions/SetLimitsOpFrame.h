#pragma once

// Copyright 2015 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "transactions/OperationFrame.h"

namespace stellar
{
class SetLimitsOpFrame : public OperationFrame
{

	std::unordered_map<AccountID, CounterpartyDetails> getCounterpartyDetails(Database& db, LedgerDelta* delta) const override;
	SourceDetails getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails) const override;

    SetLimitsResult&
    innerResult()
    {
        return mResult.tr().setLimitsResult();
    }
    SetLimitsOp const& mSetLimits;

  public:
    SetLimitsOpFrame(Operation const& op, OperationResult& res,
                      TransactionFrame& parentTx);

    bool doApply(Application& app, LedgerDelta& delta,
                 LedgerManager& ledgerManager) override;
    bool doCheckValid(Application& app) override;

    static SetLimitsResultCode
    getInnerCode(OperationResult const& res)
    {
        return res.tr().setLimitsResult().code();
    }
    bool isValidLimits();
};
}
