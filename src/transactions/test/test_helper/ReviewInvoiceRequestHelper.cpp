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
    Database& db = mTestManager->getDB();

    auto reviewableRequestHelper = ReviewableRequestHelper::Instance();
    auto request = reviewableRequestHelper->loadRequest(requestID, db);
    if (!request || request->getType() != ReviewableRequestType::INVOICE)
    {
        return;
    }
}

void
InvoiceReviewChecker::checkApprove(ReviewableRequestFrame::pointer requestBeforeTx)
{
    Database& db = mTestManager->getDB();

    auto requestAfterTx = ReviewableRequestHelper::Instance()->loadRequest(requestBeforeTx->getRequestID(), db);
    auto invoiceRequestBeforeTx = requestBeforeTx->getRequestEntry().body.invoiceRequest();
    auto invoiceRequestAfterTx = requestAfterTx->getRequestEntry().body.invoiceRequest();
    REQUIRE(!invoiceRequestBeforeTx.isSecured);
    REQUIRE(invoiceRequestAfterTx.isSecured);
    REQUIRE(invoiceRequestBeforeTx.amount == invoiceRequestAfterTx.amount);
    REQUIRE(invoiceRequestBeforeTx.sender == invoiceRequestAfterTx.sender);
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
    requestMustBeDeletedAfterApproval = false;
    return ReviewRequestHelper::applyReviewRequestTx(source, requestID,
                                                     requestHash, requestType,
                                                     action, rejectReason,
                                                     expectedResult,
                                                     checker);
}

}
}