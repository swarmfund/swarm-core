#include "ReviewChangeKYCRequestHelper.h"
#include "test/test_marshaler.h"
#include "ledger/ReviewableRequestHelper.h"
#include "ledger/AccountHelper.h"
#include "ledger/AccountKYCHelper.h"
namespace stellar {

    namespace txtest {
        void ReviewKYCRequestChecker::checkApprove(ReviewableRequestFrame::pointer pointer) {
            REQUIRE(!!request);
            REQUIRE(!!accountBeforeTx);
            auto accountAfterTx = AccountHelper::Instance()->loadAccount(request->updatedAccount, mTestManager->getDB());
            REQUIRE(!!accountAfterTx);
            REQUIRE(accountAfterTx->getAccountType() == request->accountTypeToSet);
            REQUIRE(accountAfterTx->getKYCLevel() == request->kycLevel);

            auto accountKYCAfterTx = AccountKYCHelper::Instance()->loadAccountKYC(request->updatedAccount,mTestManager->getDB());
            REQUIRE(!!accountKYCAfterTx);
            REQUIRE(accountKYCAfterTx->getKYCData() == request->kycData);
        }

        ReviewKYCRequestChecker::ReviewKYCRequestChecker(TestManager::pointer testManager, uint64_t requestID)
                : ReviewChecker(testManager) {
            auto requestFrame = ReviewableRequestHelper::Instance()->loadRequest(requestID, mTestManager->getDB());
            if (!requestFrame || requestFrame->getType() != ReviewableRequestType::CHANGE_KYC) {
                return;
            }

            request = std::make_shared<ChangeKYCRequest>(requestFrame->getRequestEntry().body.changeKYCRequest());
            accountBeforeTx = AccountHelper::Instance()->loadAccount(
                    requestFrame->getRequestEntry().body.changeKYCRequest().updatedAccount, mTestManager->getDB());
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
            auto checker = ReviewKYCRequestChecker(mTestManager, requestID);
            return ReviewRequestHelper::applyReviewRequestTx(source, requestID,
                                                             requestHash, requestType,
                                                             action, rejectReason,
                                                             expectedResult,
                                                             checker
            );
        }



    }
}