#pragma once

// Copyright 2015 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ReviewRequestOpFrame.h"
#include "ledger/ReviewableRequestFrame.h"

namespace stellar
{
class ReviewTwoStepWithdrawalRequestOpFrame : public ReviewRequestOpFrame
{
protected:
	bool handleApprove(Application& app, LedgerDelta& delta, LedgerManager& ledgerManager, ReviewableRequestFrame::pointer request) override;
	bool handleReject(Application& app, LedgerDelta& delta, LedgerManager& ledgerManager, ReviewableRequestFrame::pointer request) override;

	virtual SourceDetails getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails) const override;

        // returns total fee to pay, throws exception if overflow
        uint64_t getTotalFee(const uint64_t requestID, WithdrawalRequest& withdrawalRequest);
        // returns total amount to be charged, throws exception if overflow
        uint64_t getTotalAmountToCharge(const uint64_t requestID, WithdrawalRequest& withdrawalRequest);

        bool rejectWithdrawalRequest(Application& app, LedgerDelta& delta,
            LedgerManager& ledgerManager,
            ReviewableRequestFrame::pointer request,
            WithdrawalRequest& withdrawRequest);
public:

	  ReviewTwoStepWithdrawalRequestOpFrame(Operation const& op, OperationResult& res,
                       TransactionFrame& parentTx);
protected:
    bool handlePermanentReject(Application& app, LedgerDelta& delta,
        LedgerManager& ledgerManager,
        ReviewableRequestFrame::pointer request) override;
};
}
