#pragma once

#include "ReviewRequestOpFrame.h"

namespace stellar {
    class ReviewUpdateSaleEndTimeRequestOpFrame : public ReviewRequestOpFrame {
    protected:
        bool handleApprove(Application &app, LedgerDelta &delta, LedgerManager &ledgerManager,
                           ReviewableRequestFrame::pointer request) override;

        SourceDetails
        getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
                                int32_t ledgerVersion) const override;

    public:
        ReviewUpdateSaleEndTimeRequestOpFrame(Operation const &op, OperationResult &res, TransactionFrame &parentTx);
    };
}