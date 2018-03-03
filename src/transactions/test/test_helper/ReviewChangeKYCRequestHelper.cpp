#include "ReviewChangeKYCRequestHelper.h"
#include "test/test_marshaler.h"
#include "ledger/ReviewableRequestHelper.h"
#include "ledger/AccountHelper.h"
#include "ledger/AccountKYCHelper.h"
namespace stellar {

    namespace txtest {
        void ReviewKYCRequestChecker::checkApprove(ReviewableRequestFrame::pointer pointer) {
            auto request = pointer->getRequestEntry().body.changeKYCRequest();
            auto accountAfterTx = AccountHelper::Instance()->loadAccount(request.updatedAccount, mTestManager->getDB());
            REQUIRE(!!accountAfterTx);
            REQUIRE(accountAfterTx->getAccountType() == request.accountTypeToSet);
            REQUIRE(accountAfterTx->getKYCLevel() == request.kycLevel);

            auto accountKYCAfterTx = AccountKYCHelper::Instance()->loadAccountKYC(request.updatedAccount,mTestManager->getDB());
            REQUIRE(!!accountKYCAfterTx);
            REQUIRE(accountKYCAfterTx->getKYCData() == request.kycData);
        }

        ReviewKYCRequestChecker::ReviewKYCRequestChecker(TestManager::pointer testManager)
                : ReviewChecker(testManager) {
        }



        ReviewKYCRequestTestHelper::ReviewKYCRequestTestHelper(
                TestManager::pointer testManager) : ReviewRequestHelper(testManager)
        {
        }

        ReviewRequestResult ReviewKYCRequestTestHelper::applyReviewRequestTx(Account &source, uint64_t requestID,
                                                                             Hash requestHash,
                                                                             ReviewableRequestType requestType,
                                                                             ReviewRequestOpAction action,
                                                                             std::string rejectReason,
                                                                             ReviewRequestResultCode expectedResult) {
            auto checker = ReviewKYCRequestChecker(mTestManager);
            return ReviewRequestHelper::applyReviewRequestTx(source, requestID,
                                                             requestHash, requestType,
                                                             action, rejectReason,
                                                             expectedResult,
                                                             checker);
        }



    }
}