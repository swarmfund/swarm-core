#include "ReviewUpdateKYCRequestHelper.h"
#include "test/test_marshaler.h"
#include "ledger/ReviewableRequestHelper.h"
#include "ledger/AccountHelper.h"
#include "ledger/AccountKYCHelper.h"
namespace stellar {

    namespace txtest {
        void ReviewKYCRequestChecker::checkApprove(ReviewableRequestFrame::pointer request) {
            auto updateKYCRequest = request->getRequestEntry().body.updateKYCRequest();
            auto accountAfterTx = AccountHelper::Instance()->loadAccount(updateKYCRequest.accountToUpdateKYC, mTestManager->getDB());
            REQUIRE(!!accountAfterTx);
            REQUIRE(accountAfterTx->getAccountType() == updateKYCRequest.accountTypeToSet);
            REQUIRE(accountAfterTx->getKYCLevel() == updateKYCRequest.kycLevel);

            auto accountKYCAfterTx = AccountKYCHelper::Instance()->loadAccountKYC(updateKYCRequest.accountToUpdateKYC,mTestManager->getDB());
            REQUIRE(!!accountKYCAfterTx);
            REQUIRE(accountKYCAfterTx->getKYCData() == updateKYCRequest.kycData);
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

        TransactionFramePtr
        ReviewKYCRequestTestHelper::createReviewRequestTx(Account& source, uint64_t requestID, Hash requestHash,
                                                          ReviewableRequestType requestType,
                                                          ReviewRequestOpAction action, std::string rejectReason) {
            Operation op;
            op.body.type(OperationType::REVIEW_REQUEST);
            ReviewRequestOp& reviewRequestOp = op.body.reviewRequestOp();
            reviewRequestOp.action = action;
            reviewRequestOp.reason = rejectReason;
            reviewRequestOp.requestHash = requestHash;
            reviewRequestOp.requestID = requestID;
            reviewRequestOp.requestDetails.requestType(requestType);
            reviewRequestOp.requestDetails.updateKYC().newTasks = 0;
            reviewRequestOp.requestDetails.updateKYC().externalDetails = "{}";

            return txFromOperation(source, op, nullptr);
        }

    }
}