#pragma once

// Copyright 2015 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "transactions/OperationFrame.h"
#include "ledger/ReviewableRequestFrame.h"

namespace stellar
{
class ReviewRequestOpFrame : public OperationFrame
{
	bool isRejectReasonValid();
protected:
    ReviewRequestResult&
    innerResult()
    {
        return mResult.tr().reviewRequestResult();
    }

    ReviewRequestOp const& mReviewRequest;

	virtual std::unordered_map<AccountID, CounterpartyDetails> getCounterpartyDetails(Database& db, LedgerDelta* delta) const override;
	virtual SourceDetails getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
                                                      int32_t ledgerVersion) const override
	{
		// no one is allowed to run default implementation of ReviewRequestOp
		return SourceDetails({}, mSourceAccount->getHighThreshold(), 0);
	}

	virtual bool handleReject(Application& app, LedgerDelta& delta, LedgerManager& ledgerManager, ReviewableRequestFrame::pointer request);
	virtual bool handlePermanentReject(Application& app, LedgerDelta& delta, LedgerManager& ledgerManager, ReviewableRequestFrame::pointer request);
	virtual bool handleApprove(Application& app, LedgerDelta& delta, LedgerManager& ledgerManager, ReviewableRequestFrame::pointer request) {
		throw std::runtime_error("There is no default implementation of handleApprove of Reviewable Request");
	}

	// creates reference entry, throws excpetion if reference already exist. Throws expception if reference is null
	void createReference(LedgerDelta& delta, Database& db, AccountID const& requestor, xdr::pointer<stellar::string64> reference);

  public:

    ReviewRequestOpFrame(Operation const& op, OperationResult& res,
                       TransactionFrame& parentTx);

	static ReviewRequestOpFrame* makeHelper(Operation const& op, OperationResult& res, TransactionFrame& parentTx);

    bool doApply(Application& app, LedgerDelta& delta,
                 LedgerManager& ledgerManager) override;
    bool doCheckValid(Application& app) override;

    static ReviewRequestResultCode
    getInnerCode(OperationResult const& res)
    {
        return res.tr().reviewRequestResult().code();
    }

	std::string getInnerResultCodeAsStr() override {
		return xdr::xdr_traits<ReviewRequestResultCode>::enum_name(innerResult().code());
	}

    static uint64_t getTotalFee(uint64_t requestID, Fee fee); 
};
}
