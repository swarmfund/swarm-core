#pragma once

// Copyright 2015 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "transactions/OperationFrame.h"
#include "ledger/CoinsEmissionRequestFrame.h"

namespace stellar
{
class ReviewCoinsEmissionRequestOpFrame : public OperationFrame
{
    ReviewCoinsEmissionRequestResult&
    innerResult()
    {
        return mResult.tr().reviewCoinsEmissionRequestResult();
    }

    ReviewCoinsEmissionRequestOp const& mReviewCoinsEmissionRequest;

	CoinsEmissionRequestFrame::pointer getRequest(Application& app, LedgerDelta& delta);

	std::unordered_map<AccountID, CounterpartyDetails> getCounterpartyDetails(Database& db, LedgerDelta* delta) const override;
	SourceDetails getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails) const override;

  public:

    ReviewCoinsEmissionRequestOpFrame(Operation const& op, OperationResult& res,
                       TransactionFrame& parentTx);

    bool doApply(Application& app, LedgerDelta& delta,
                 LedgerManager& ledgerManager) override;
    bool doCheckValid(Application& app) override;

    static ReviewCoinsEmissionRequestResultCode
    getInnerCode(OperationResult const& res)
    {
        return res.tr().reviewCoinsEmissionRequestResult().code();
    }
};
}
