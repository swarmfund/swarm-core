#pragma once

// Copyright 2015 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ReviewRequestOpFrame.h"
#include "ledger/ReviewableRequestFrame.h"

namespace stellar
{
class ReviewWithdrawalRequestOpFrame : public ReviewRequestOpFrame
{
protected:
	bool handleApprove(Application& app, LedgerDelta& delta, LedgerManager& ledgerManager, ReviewableRequestFrame::pointer request) override;
	bool handleReject(Application& app, LedgerDelta& delta, LedgerManager& ledgerManager, ReviewableRequestFrame::pointer request) override;

	virtual SourceDetails getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails) const override;

        // returns total fee to pay, throws exception if overflow
        uint64_t getTotalFee(const uint64_t requestID, WithdrawalRequest& withdrawalRequest);
        // returns total amount to be charged, throws exception if overflow
        uint64_t getTotalAmountToCharge(const uint64_t requestID, WithdrawalRequest& withdrawalRequest);

        void transferFee(Application& app, Database& db, LedgerDelta& delta, ReviewableRequestFrame::pointer request, BalanceFrame::pointer balance);
  public:

	  ReviewWithdrawalRequestOpFrame(Operation const& op, OperationResult& res,
                       TransactionFrame& parentTx);
protected:
    bool handlePermanentReject(Application& app, LedgerDelta& delta,
        LedgerManager& ledgerManager,
        ReviewableRequestFrame::pointer request) override;
};
}