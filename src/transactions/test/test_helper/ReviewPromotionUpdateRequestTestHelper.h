#pragma once

#include "overlay/StellarXDR.h"
#include "ReviewRequestTestHelper.h"
#include "ledger/ReviewableRequestFrame.h"
#include "ledger/AssetFrame.h"

namespace stellar {
    namespace txtest {
        class PromotionUpdateReviewChecker : public ReviewChecker {
            std::shared_ptr<PromotionUpdateRequest> promotionUpdateRequest;
            AssetFrame::pointer baseAssetBeforeTx;
            SaleFrame::pointer saleBeforeTx;
        public:
            PromotionUpdateReviewChecker(TestManager::pointer testManager, uint64_t requestID);

            void checkApprove(ReviewableRequestFrame::pointer) override;
        };

        class ReviewPromotionUpdateRequestHelper : public ReviewRequestHelper {
        public:
            explicit ReviewPromotionUpdateRequestHelper(TestManager::pointer testManager);

            using ReviewRequestHelper::applyReviewRequestTx;

            ReviewRequestResult applyReviewRequestTx(Account &source,
                                                     uint64_t requestID,
                                                     Hash requestHash,
                                                     ReviewableRequestType requestType,
                                                     ReviewRequestOpAction action,
                                                     std::string rejectReason,
                                                     ReviewRequestResultCode
                                                     expectedResult =
                                                     ReviewRequestResultCode::
                                                     SUCCESS) override;
        };
    }
}