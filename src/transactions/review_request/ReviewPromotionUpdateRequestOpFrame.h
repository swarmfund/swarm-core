#pragma once

#include "ReviewSaleCreationRequestOpFrame.h"

namespace stellar {
    class ReviewPromotionUpdateRequestOpFrame : public ReviewSaleCreationRequestOpFrame {
    protected:
        bool handleApprove(Application &app, LedgerDelta &delta, LedgerManager &ledgerManager,
                           ReviewableRequestFrame::pointer request) override;

        SourceDetails
        getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
                                int32_t ledgerVersion) const override;

    public:
        ReviewPromotionUpdateRequestOpFrame(Operation const &op, OperationResult &res, TransactionFrame &parentTx);
    };
}