#pragma once

#include "ReviewRequestOpFrame.h"
#include "ledger/ReviewableRequestFrame.h"

namespace stellar
{
class ReviewIssuanceCreationRequestOpFrame : public ReviewRequestOpFrame
{
protected:
	bool handleApprove(Application& app, LedgerDelta& delta, LedgerManager& ledgerManager,
					   ReviewableRequestFrame::pointer request) override;

	bool handleApproveV1(Application& app, LedgerDelta& delta, LedgerManager& ledgerManager,
						 ReviewableRequestFrame::pointer request);
	bool handleApproveV2(Application& app, LedgerDelta& delta, LedgerManager& ledgerManager,
						 ReviewableRequestFrame::pointer request);

	bool handleReject(Application& app, LedgerDelta& delta, LedgerManager& ledgerManager,
					  ReviewableRequestFrame::pointer request) override;

	virtual SourceDetails getSourceAccountDetails(std::unordered_map<AccountID,
			CounterpartyDetails> counterpartiesDetails, int32_t ledgerVersion) const override;
public:

    ReviewIssuanceCreationRequestOpFrame(Operation const& op, OperationResult& res, TransactionFrame& parentTx);

    bool doCheckValid(Application &app) override;
};
}
