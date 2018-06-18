// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include <ledger/StatisticsHelper.h>
#include <ledger/AssetPairHelper.h>
#include <transactions/CreateManageLimitsRequestOpFrame.h>
#include "LimitsUpdateRequestHelper.h"
#include "ledger/AssetHelper.h"
#include "ledger/BalanceHelper.h"
#include "ledger/ReviewableRequestHelper.h"
#include "ledger/AccountLimitsHelper.h"
#include "transactions/SetOptionsOpFrame.h"
#include "test/test_marshaler.h"

namespace stellar
{
namespace txtest
{
LimitsUpdateRequestHelper::
LimitsUpdateRequestHelper(TestManager::pointer testManager) : TxHelper(testManager)
{
}

CreateManageLimitsRequestResult
LimitsUpdateRequestHelper::applyCreateLimitsUpdateRequest(Account &source, LimitsUpdateRequest request,
                                                          CreateManageLimitsRequestResultCode expectedResult)
{
    Database& db = mTestManager->getDB();

    auto reviewableRequestHelper = ReviewableRequestHelper::Instance();
    uint64 reviewableRequestCountBeforeTx = reviewableRequestHelper->countObjects(db.getSession());

    auto txFrame = createLimitsUpdateRequestTx(source, request);
    mTestManager->applyCheck(txFrame);
    auto txResult = txFrame->getResult();
    auto opResult = txResult.result.results()[0];

    auto actualResultCode = CreateManageLimitsRequestOpFrame::getInnerCode(opResult);
    REQUIRE(actualResultCode == expectedResult);

    uint64 reviewableRequestCountAfterTx = reviewableRequestHelper->countObjects(db.getSession());
    if (expectedResult != CreateManageLimitsRequestResultCode::SUCCESS)
    {
        REQUIRE(reviewableRequestCountBeforeTx == reviewableRequestCountAfterTx);
        return CreateManageLimitsRequestResult{};
    }

    CreateManageLimitsRequestResult createManageLimitsRequestResult = opResult.tr().createManageLimitsRequestResult();
    auto limitsUpdateRequest = reviewableRequestHelper->loadRequest(
            createManageLimitsRequestResult.success().manageLimitsRequestID, db);
    REQUIRE(!!limitsUpdateRequest);
    REQUIRE(reviewableRequestCountBeforeTx + 1 == reviewableRequestCountAfterTx);

    return createManageLimitsRequestResult;
}

LimitsUpdateRequest
LimitsUpdateRequestHelper::createLimitsUpdateRequest(Hash documentHash)
{
    LimitsUpdateRequest result;
    result.documentHash = documentHash;
    result.ext.v(LedgerVersion::EMPTY_VERSION);
    return result;
}

TransactionFramePtr
LimitsUpdateRequestHelper::createLimitsUpdateRequestTx(Account& source,
                                                       LimitsUpdateRequest request)
{
    Operation baseOp;
    baseOp.body.type(OperationType::CREATE_MANAGE_LIMITS_REQUEST);
    auto& op = baseOp.body.createManageLimitsRequestOp();
    op.manageLimitsRequest.documentHash = request.documentHash;
    op.ext.v(LedgerVersion::EMPTY_VERSION);
    return txFromOperation(source, baseOp, nullptr);
}

}
}