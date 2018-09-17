#include <ledger/BalanceHelper.h>
#include "PayoutTestHelper.h"
#include "transactions/PayoutOpFrame.h"
#include "test/test_marshaler.h"

namespace stellar
{
namespace txtest
{
PayoutTestHelper::PayoutTestHelper(TestManager::pointer testManager)
        : TxHelper(testManager)
{
}

TransactionFramePtr
PayoutTestHelper::createPayoutTx(Account &source, AssetCode asset,
             BalanceID sourceBalanceID, uint64_t maxPayoutAmount,
             uint64_t minPayOutAmount, uint64_t minAssetHolderAmount, Fee &fee)
{
    Operation op;
    op.body.type(OperationType::PAYOUT);

    PayoutOp& payoutOp = op.body.payoutOp();
    payoutOp.asset = asset;
    payoutOp.sourceBalanceID = sourceBalanceID;
    payoutOp.maxPayoutAmount = maxPayoutAmount;
    payoutOp.minPayoutAmount = minPayOutAmount;
    payoutOp.minAssetHolderAmount = minAssetHolderAmount;
    payoutOp.fee = fee;

    return TxHelper::txFromOperation(source, op, nullptr);
}

PayoutResult
PayoutTestHelper::applyPayoutTx(Account &source, AssetCode asset,
            BalanceID sourceBalanceID, uint64_t maxPayoutAmount,
            uint64_t minPayOutAmount, uint64_t minAssetHolderAmount, Fee &fee,
            PayoutResultCode expectedResult)
{
    Database& db = mTestManager->getDB();
    auto balanceHelper = BalanceHelper::Instance();
    auto ownerBalanceBefore = balanceHelper->loadBalance(sourceBalanceID, db);
    BalanceFrame::pointer commissionBalanceBefore;
    if (ownerBalanceBefore != nullptr)
        commissionBalanceBefore = balanceHelper->
            loadBalance(mTestManager->getApp().getCommissionID(),
                        ownerBalanceBefore->getAsset(), db);

    auto assetHoldersBefore = balanceHelper->loadAssetHolders(asset,
            source.key.getPublicKey(), minAssetHolderAmount, db);

    TransactionFramePtr txFrame;
    txFrame = createPayoutTx(source, asset, sourceBalanceID,
            maxPayoutAmount, minPayOutAmount, minAssetHolderAmount, fee);
    mTestManager->applyCheck(txFrame);

    auto opResult = txFrame->getResult().result.results()[0];
    REQUIRE(PayoutOpFrame::getInnerCode(opResult) == expectedResult);

    auto result = opResult.tr().payoutResult();
    if (result.code() != PayoutResultCode::SUCCESS)
        return PayoutResult{};

    auto actualPayoutAmount = result.payoutSuccessResult().actualPayoutAmount;
    REQUIRE(actualPayoutAmount < maxPayoutAmount);

    auto totalFee = fee.fixed + fee.percent;

    auto ownerBalanceAfter = balanceHelper->loadBalance(sourceBalanceID, db);
    REQUIRE(ownerBalanceBefore->getAmount() ==
            ownerBalanceAfter->getAmount() + actualPayoutAmount + totalFee);

    auto assetHoldersAfter = balanceHelper->loadAssetHolders(asset,
            source.key.getPublicKey(), minAssetHolderAmount, db);

    for (auto response : result.payoutSuccessResult().payoutResponses)
    {
        uint64_t amountBefore = 0;
        for (auto balance : assetHoldersBefore)
            if (balance->getBalanceID() == response.receiverBalanceID)
            {
                amountBefore = balance->getAmount();
                break;
            }

        for (auto balance : assetHoldersAfter)
            if (balance->getBalanceID() == response.receiverBalanceID)
            {
                REQUIRE(balance->getAmount() ==
                        response.receivedAmount + amountBefore);
                break;
            }
    }

    BalanceFrame::pointer commissionBalanceAfter;
    if (ownerBalanceBefore != nullptr)
        commissionBalanceAfter = balanceHelper->
            loadBalance(mTestManager->getApp().getCommissionID(),
                        ownerBalanceBefore->getAsset(), db);

    uint64_t commissionAmountBefore = 0;
    if (commissionBalanceBefore != nullptr)
        commissionAmountBefore = commissionBalanceBefore->getAmount();

    uint64_t commissionAmountAfter = 0;
    if (commissionBalanceAfter != nullptr)
        commissionAmountAfter = commissionBalanceAfter->getAmount();

    REQUIRE(commissionAmountAfter == commissionAmountBefore + totalFee);

    return result;
}
}
}
