// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "WithdrawRequestHelper.h"
#include "ledger/AssetHelper.h"
#include "ledger/BalanceHelper.h"
#include "ledger/ReviewableRequestHelper.h"
#include "ReviewPreIssuanceRequestHelper.h"
#include "ManageAssetTestHelper.h"
#include "transactions/CreateWithdrawalRequestOpFrame.h"


namespace stellar
{
namespace txtest
{
WithdrawRequestHelper::
WithdrawRequestHelper(TestManager::pointer testManager) : TxHelper(testManager)
{
}

CreateWithdrawalRequestResult WithdrawRequestHelper::applyCreateWithdrawRequest(
    Account& source, WithdrawalRequest request,
    CreateWithdrawalRequestResultCode expectedResult)
{
    auto reviewableRequestHelper = ReviewableRequestHelper::Instance();
    auto reviewableRequestCountBeforeTx = reviewableRequestHelper->countObjects(mTestManager->getDB().getSession());
    auto balanceBeforeRequest = BalanceHelper::Instance()->loadBalance(request.balance, mTestManager->getDB());
    

    auto txFrame = createWithdrawalRequestTx(source, request);
    mTestManager->applyCheck(txFrame);
    auto txResult = txFrame->getResult();
    auto opResult = txResult.result.results()[0];
    auto actualResultCode = CreateWithdrawalRequestOpFrame::getInnerCode(opResult);
    REQUIRE(actualResultCode == expectedResult);

    uint64 reviewableRequestCountAfterTx = reviewableRequestHelper->countObjects(mTestManager->getDB().getSession());
    if (expectedResult != CreateWithdrawalRequestResultCode::SUCCESS)
    {
        REQUIRE(reviewableRequestCountBeforeTx == reviewableRequestCountAfterTx);
        return CreateWithdrawalRequestResult{};
    }

    REQUIRE(!!balanceBeforeRequest);
    REQUIRE(reviewableRequestCountBeforeTx + 1 == reviewableRequestCountAfterTx);

    auto balanceAfterRequest = BalanceHelper::Instance()->loadBalance(request.balance, mTestManager->getDB());
    REQUIRE(!!balanceAfterRequest);
    REQUIRE(balanceBeforeRequest->getAmount() == balanceAfterRequest->getAmount() + request.amount + request.fee.fixed + request.fee.percent);
    REQUIRE(balanceAfterRequest->getLocked() == balanceBeforeRequest->getLocked() + request.amount + request.fee.fixed + request.fee.percent);

    return opResult.tr().createWithdrawalRequestResult();
}

WithdrawalRequest WithdrawRequestHelper::createWithdrawRequest(
    const BalanceID balance, const uint64_t amount, const Fee fee, std::string externalDetails,
    AssetCode autoConversionDestAsset, const uint64_t expectedAutoConversion)
{
    WithdrawalRequest result;
    result.balance = balance;
    result.amount = amount;
    result.fee = fee;
    result.externalDetails = externalDetails;
    result.details.withdrawalType(WithdrawalType::AUTO_CONVERSION);
    result.details.autoConversion().destAsset = autoConversionDestAsset;
    result.details.autoConversion().expectedAmount = expectedAutoConversion;
    result.ext.v(LedgerVersion::EMPTY_VERSION);
    return result;
}

TransactionFramePtr WithdrawRequestHelper::createWithdrawalRequestTx(
    Account& source, const WithdrawalRequest request)
{
    Operation baseOp;
    baseOp.body.type(OperationType::CREATE_WITHDRAWAL_REQUEST);
    auto& op = baseOp.body.createWithdrawalRequestOp();
    op.request = request;
    op.ext.v(LedgerVersion::EMPTY_VERSION);
    return txFromOperation(source, baseOp, nullptr);
}
}
}
