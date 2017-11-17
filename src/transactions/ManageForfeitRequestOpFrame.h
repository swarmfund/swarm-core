#pragma once

// Copyright 2015 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "transactions/OperationFrame.h"

namespace stellar
{
class ManageForfeitRequestOpFrame : public OperationFrame
{

    ManageForfeitRequestResult&
    innerResult()
    {
        return mResult.tr().manageForfeitRequestResult();
    }
    ManageForfeitRequestOp const& mManageForfeitRequest;

	std::unordered_map<AccountID, CounterpartyDetails> getCounterpartyDetails(Database& db, LedgerDelta* delta) const override;
	SourceDetails getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails) const override;

    int64_t calculateFee(Database &db, AssetCode asset, int64_t amount);
public:

    ManageForfeitRequestOpFrame(Operation const& op, OperationResult& res,
                      TransactionFrame& parentTx);
    bool doApply(Application& app, LedgerDelta& delta,
                 LedgerManager& ledgerManager) override;

    bool doCheckValid(Application& app) override;


    bool
    processBalanceChange(Application& app,
        AccountManager::Result balanceChangeResult);

    static ManageForfeitRequestResultCode
    getInnerCode(OperationResult const& res)
    {
        return res.tr().manageForfeitRequestResult().code();
    }
};
}
