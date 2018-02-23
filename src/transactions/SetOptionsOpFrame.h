#pragma once

// Copyright 2015 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "transactions/OperationFrame.h"

namespace stellar
{
class SetOptionsOpFrame : public OperationFrame
{
	std::unordered_map<AccountID, CounterpartyDetails> getCounterpartyDetails(Database& db, LedgerDelta* delta) const override;
	SourceDetails getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
                                              int32_t ledgerVersion) const override;

    SetOptionsResult&
    innerResult()
    {
        return mResult.tr().setOptionsResult();
    }
    SetOptionsOp const& mSetOptions;

	// returns false if error occurs
	bool tryUpdateSigners(Application& app, LedgerManager& ledgerManager);
    bool tryCreateUpdateLimitsRequest(Application& app, LedgerDelta& delta, LedgerManager& ledgerManager);

    std::string getLimitsUpdateRequestReference(Hash const& documentHash) const;

  public:
    SetOptionsOpFrame(Operation const& op, OperationResult& res,
                      TransactionFrame& parentTx);

    bool doApply(Application& app, LedgerDelta& delta,
                 LedgerManager& ledgerManager) override;
    bool doCheckValid(Application& app) override;

    static SetOptionsResultCode
    getInnerCode(OperationResult const& res)
    {
        return res.tr().setOptionsResult().code();
    }

    std::string getInnerResultCodeAsStr() override;
};
}
