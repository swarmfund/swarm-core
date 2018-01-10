//
// Created by volodymyr on 07.01.18.
//

#include <ledger/ReviewableRequestHelper.h>
#include <ledger/AccountHelper.h>
#include <ledger/AccountKYCHelper.h>
#include "ReviewKYCRequestTestHelper.h"
#include "test/test_marshaler.h"

namespace stellar {

namespace txtest
{

ReviewKYCRequestChecker::ReviewKYCRequestChecker(TestManager::pointer testManager, uint64_t requestID)
    : ReviewChecker(testManager)
{
    auto requestFrame = ReviewableRequestHelper::Instance()->loadRequest(requestID, mTestManager->getDB());
    if (!requestFrame || requestFrame->getRequestType() != ReviewableRequestType::UPDATE_KYC)
        return;

    updateKYCRequest = std::make_shared<UpdateKYCRequest>(requestFrame->getRequestEntry().body.updateKYCRequest());

    accountBeforeTx = AccountHelper::Instance()->mustLoadAccount(requestFrame->getRequestor(), mTestManager->getDB());
}

void ReviewKYCRequestChecker::checkApprove(ReviewableRequestFrame::pointer)
{
    REQUIRE(!!updateKYCRequest);

    // before review account is NOT_VERIFIED:
    REQUIRE(accountBeforeTx->getAccountType() == AccountType::NOT_VERIFIED);

    //after review account type restored to original state
    auto accountAfterTx = AccountHelper::Instance()->mustLoadAccount(accountBeforeTx->getID(), mTestManager->getDB());
    REQUIRE(accountAfterTx->getAccountType() == updateKYCRequest->accountTypeBeforeUpdate);

    //KYCData entry was created:
    auto KYCData = AccountKYCHelper::Instance()->loadAccountKYC(accountBeforeTx->getID(), mTestManager->getDB());
    REQUIRE(KYCData);
    REQUIRE(KYCData->getKYCData() == updateKYCRequest->dataKYC);
}

ReviewKYCRequestTestHelper::ReviewKYCRequestTestHelper(TestManager::pointer testManager)
    : ReviewRequestHelper(testManager)
{
}

ReviewRequestResult
ReviewKYCRequestTestHelper::applyReviewRequestTx(Account &source, uint64_t requestID, Hash requestHash,
                                                 ReviewableRequestType requestType, ReviewRequestOpAction action,
                                                 std::string rejectReason, ReviewRequestResultCode expectedResult)
{
    ReviewKYCRequestChecker checker(mTestManager, requestID);

    return ReviewRequestHelper::applyReviewRequestTx(source, requestID, requestHash, requestType,
                                                     action, rejectReason, expectedResult, checker);
}

ReviewRequestResult
ReviewKYCRequestTestHelper::applyReviewRequestTx(Account &source, uint64_t requestID, ReviewRequestOpAction action,
                                                 std::string rejectReason, ReviewRequestResultCode expectedResult)
{
    auto request = ReviewableRequestHelper::Instance()->loadRequest(requestID, mTestManager->getDB());
    REQUIRE(request);

    return applyReviewRequestTx(source, requestID, request->getHash(), request->getRequestType(),
                                action, rejectReason, expectedResult);
}

}
}