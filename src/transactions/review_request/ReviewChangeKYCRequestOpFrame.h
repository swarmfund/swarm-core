#pragma once
#include "ReviewRequestOpFrame.h"
namespace stellar {


	class ReviewChangeKYCRequestOpFrame : public ReviewRequestOpFrame {
	protected:
		bool handleApprove(Application& app, LedgerDelta& delta, LedgerManager& ledgerManager, ReviewableRequestFrame::pointer request) override;

		
		SourceDetails
			getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
									int32_t ledgerVersion) const override;
	public:
		ReviewChangeKYCRequestOpFrame(Operation const& op, OperationResult& res, TransactionFrame& parentTx);
	};
}