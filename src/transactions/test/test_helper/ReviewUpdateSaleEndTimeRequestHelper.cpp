#include "ReviewUpdateSaleEndTimeRequestHelper.h"
#include <ledger/SaleHelper.h>
#include <lib/catch.hpp>

namespace stellar {
    namespace txtest {
        ReviewUpdateSaleEndTimeRequestChecker::ReviewUpdateSaleEndTimeRequestChecker(TestManager::pointer testManager)
                : ReviewChecker(testManager) {

        }

        void ReviewUpdateSaleEndTimeRequestChecker::checkApprove(ReviewableRequestFrame::pointer request) {
            auto updateSaleEndTimeRequest = request->getRequestEntry().body.updateSaleEndTimeRequest();
            auto saleAfterTx = SaleHelper::Instance()->loadSale(updateSaleEndTimeRequest.saleID, mTestManager->getDB());
            REQUIRE(!!saleAfterTx);
            REQUIRE(saleAfterTx->getSaleEntry().endTime == updateSaleEndTimeRequest.newEndTime);
        }


        ReviewUpdateSaleEndTimeRequestHelper::ReviewUpdateSaleEndTimeRequestHelper(TestManager::pointer testManager)
                : ReviewRequestHelper(testManager) {
        }

        ReviewRequestResult
        ReviewUpdateSaleEndTimeRequestHelper::applyReviewRequestTx(Account &source, uint64_t requestID,
                                                                   Hash requestHash, ReviewableRequestType requestType,
                                                                   ReviewRequestOpAction action,
                                                                   std::string rejectReason,
                                                                   ReviewRequestResultCode expectedResult) {
            auto checker = ReviewUpdateSaleEndTimeRequestChecker(mTestManager);
            return ReviewRequestHelper::applyReviewRequestTx(source, requestID, requestHash, requestType, action,
                                                             rejectReason, expectedResult, checker);
        }
    }
}