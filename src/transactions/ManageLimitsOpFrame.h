#pragma once

// Copyright 2015 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "transactions/OperationFrame.h"

namespace stellar
{
class ManageLimitsOpFrame : public OperationFrame
{

	std::unordered_map<AccountID, CounterpartyDetails> getCounterpartyDetails(Database& db, LedgerDelta* delta) const override;
	SourceDetails getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
                                              int32_t ledgerVersion) const override;

    ManageLimitsResult&
    innerResult()
    {
        return mResult.tr().manageLimitsResult();
    }
    ManageLimitsOp const& mManageLimits;

  public:
    ManageLimitsOpFrame(Operation const& op, OperationResult& res,
                      TransactionFrame& parentTx);

    bool doApply(Application& app, LedgerDelta& delta,
                 LedgerManager& ledgerManager) override;
    bool doCheckValid(Application& app) override;

    static ManageLimitsResultCode
    getInnerCode(OperationResult const& res)
    {
        return res.tr().manageLimitsResult().code();
    }

    std::string getInnerResultCodeAsStr() override;

    bool isValidLimits();
};
}
