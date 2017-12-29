// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "SaleRequestHelper.h"
#include "ledger/ReviewableRequestHelper.h"
#include "ledger/SaleHelper.h"
#include "ReviewSaleRequestHelper.h"
#include "test/test_marshaler.h"

namespace stellar
{
namespace txtest
{
SaleRequestHelper::SaleRequestHelper(const TestManager::pointer testManager) : TxHelper(testManager)
{
}

void SaleRequestHelper::createApprovedSale(Account& root, Account& source,
                                           const SaleCreationRequest request)
{
    auto requestCreationResult = applyCreateSaleRequest(source, 0, request);
    auto reviewer = ReviewSaleRequestHelper(mTestManager);
    reviewer.applyReviewRequestTx(root, requestCreationResult.success().requestID, ReviewRequestOpAction::APPROVE, "");
}

CreateSaleCreationRequestResult SaleRequestHelper::applyCreateSaleRequest(
    Account& source, const uint64_t requestID, const SaleCreationRequest request,
    CreateSaleCreationRequestResultCode expectedResult)
{
    auto reviewableRequestHelper = ReviewableRequestHelper::Instance();
    auto reviewableRequestCountBeforeTx = reviewableRequestHelper->countObjects(mTestManager->getDB().getSession());


    auto txFrame = createSaleRequestTx(source, requestID, request);
    mTestManager->applyCheck(txFrame);
    auto txResult = txFrame->getResult();
    auto opResult = txResult.result.results()[0];
    auto actualResultCode = CreateSaleCreationRequestOpFrame::getInnerCode(opResult);
    REQUIRE(actualResultCode == expectedResult);

    auto reviewableRequestCountAfterTx = reviewableRequestHelper->countObjects(mTestManager->getDB().getSession());
    if (expectedResult != CreateSaleCreationRequestResultCode::SUCCESS)
    {
        REQUIRE(reviewableRequestCountBeforeTx == reviewableRequestCountAfterTx);
        return CreateSaleCreationRequestResult{};
    }

    if (requestID == 0)
    {
        REQUIRE(reviewableRequestCountBeforeTx + 1 == reviewableRequestCountAfterTx);
    } else
    {
        REQUIRE(reviewableRequestCountBeforeTx == reviewableRequestCountAfterTx);
    }

    return opResult.tr().createSaleCreationRequestResult();
}

SaleCreationRequest SaleRequestHelper::createSaleRequest(AssetCode base,
    AssetCode quote, const uint64_t startTime, const uint64_t endTime,
    const uint64_t price, const uint64_t softCap, const uint64_t hardCap, std::string details)
{
    SaleCreationRequest request;
    request.baseAsset = base;
    request.quoteAsset = quote;
    request.startTime = startTime;
    request.endTime = endTime;
    request.price = price;
    request.softCap = softCap;
    request.hardCap = hardCap;
    request.details = details;
    return request;
}

TransactionFramePtr SaleRequestHelper::createSaleRequestTx(Account& source, const uint64_t requestID,
                                                           const SaleCreationRequest request)
{
    Operation baseOp;
    baseOp.body.type(OperationType::CREATE_SALE_REQUEST);
    auto& op = baseOp.body.createSaleCreationRequestOp();
    op.request = request;
    op.requestID = requestID;
    op.ext.v(LedgerVersion::EMPTY_VERSION);
    return txFromOperation(source, baseOp, nullptr);
}
}
}
