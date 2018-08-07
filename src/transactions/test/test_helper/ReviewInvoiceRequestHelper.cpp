// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ledger/ReviewableRequestHelper.h"
#include "test/test_marshaler.h"
#include "ReviewInvoiceRequestHelper.h"

namespace stellar
{
namespace txtest
{

InvoiceReviewChecker::InvoiceReviewChecker(TestManager::pointer testManager,
                                           uint64_t requestID) : ReviewChecker(testManager)
{
}

void
InvoiceReviewChecker::checkPermanentReject(ReviewableRequestFrame::pointer request)
{
}

ReviewInvoiceRequestHelper::ReviewInvoiceRequestHelper(
        TestManager::pointer testManager) : ReviewRequestHelper(testManager)
{
}

ReviewRequestResult
ReviewInvoiceRequestHelper::applyReviewRequestTx(Account &source, uint64_t requestID, Hash requestHash,
                                                  ReviewableRequestType requestType,
                                                  ReviewRequestOpAction action,
                                                  std::string rejectReason,
                                                  ReviewRequestResultCode expectedResult)
{
    auto checker = InvoiceReviewChecker(mTestManager, requestID);
    requestMustBeDeletedAfterApproval = true;
    auto request = ReviewableRequestHelper::Instance()->loadRequest(requestID, mTestManager->getDB());
    if (request && (request->getType() == ReviewableRequestType::INVOICE) &&
        request->getRequestEntry().body.invoiceRequest().contractID)
        requestMustBeDeletedAfterApproval = false;

    return ReviewRequestHelper::applyReviewRequestTx(source, requestID,
                                                     requestHash, requestType,
                                                     action, rejectReason,
                                                     expectedResult,
                                                     checker);
}

TransactionFramePtr
ReviewInvoiceRequestHelper::createReviewRequestTx(Account& source,
                                              uint64_t requestID, Hash requestHash,
                                              ReviewableRequestType requestType,
                                              ReviewRequestOpAction action,
                                              std::string rejectReason)
{
    Operation op;
    op.body.type(OperationType::REVIEW_REQUEST);
    ReviewRequestOp& reviewRequestOp = op.body.reviewRequestOp();
    reviewRequestOp.action = action;
    reviewRequestOp.reason = rejectReason;
    reviewRequestOp.requestHash = requestHash;
    reviewRequestOp.requestID = requestID;
    reviewRequestOp.requestDetails.requestType(requestType);
    reviewRequestOp.requestDetails.billPay().paymentDetails = paymentDetails;
    return txFromOperation(source, op, nullptr);
}

void
ReviewInvoiceRequestHelper::initializePaymentDetails(PaymentOpV2::_destination_t& destination, uint64_t amount,
                                                     PaymentFeeDataV2& feeData, std::string subject,
                                                     std::string reference, BalanceID sourceBalance)
{
    paymentDetails.amount = amount;
    paymentDetails.subject = subject;
    paymentDetails.reference = reference;
    paymentDetails.destination = destination;
    paymentDetails.feeData = feeData;
    paymentDetails.sourceBalanceID = sourceBalance;
}

}
}