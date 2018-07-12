#pragma once

#include "ReviewRequestTestHelper.h"

namespace stellar {
    namespace txtest {
        class ReviewUpdateSaleEndTimeRequestChecker : public ReviewChecker {
        public:
            explicit ReviewUpdateSaleEndTimeRequestChecker(TestManager::pointer testManager);

            void checkApprove(ReviewableRequestFrame::pointer request) override;
        };

        class ReviewUpdateSaleEndTimeRequestHelper : public ReviewRequestHelper {
        public:
            explicit ReviewUpdateSaleEndTimeRequestHelper(TestManager::pointer testManager);

            ReviewRequestResult
            applyReviewRequestTx(Account &source, uint64_t requestID, Hash requestHash,
                                 ReviewableRequestType requestType,
                                 ReviewRequestOpAction action, std::string rejectReason,
                                 ReviewRequestResultCode expectedResult) override;

            using ReviewRequestHelper::applyReviewRequestTx;
        };
    }
}