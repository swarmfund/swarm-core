#pragma once

// Copyright 2015 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "transactions/OperationFrame.h"
#include "ledger/ReviewableRequestFrame.h"

namespace stellar
{
class SetOptionsOpFrame : public OperationFrame
{
    const std::string updateKYCReference = "updateKYC";

	std::unordered_map<AccountID, CounterpartyDetails> getCounterpartyDetails(Database& db, LedgerDelta* delta) const override;
	SourceDetails getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails) const override;

    SetOptionsResult&
    innerResult()
    {
        return mResult.tr().setOptionsResult();
    }
    SetOptionsOp const& mSetOptions;

	// returns false if error occurs
	bool tryUpdateSigners(Application& app, LedgerManager& ledgerManager);

    bool processTrustData(Application &app, LedgerDelta &delta);

    void updateThresholds();

    bool updateKYC(Application &app, LedgerDelta &delta, LedgerManager &ledgerManager, uint64_t &requestID);

    ReviewableRequestFrame::pointer getUpdatedOrCreateReviewableRequest(Application &app, LedgerDelta &delta,
                                                                        LedgerManager &ledgerManager);

    ReviewableRequestFrame::pointer getOrCreateReviewableRequest(Application &app, LedgerDelta &delta,
                                                                 LedgerManager &ledgerManager);

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
};
}
