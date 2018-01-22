#include "PayoutTestHelper.h"
#include "transactions/PayoutOpFrame.h"
#include "test/test_marshaler.h"

namespace stellar {
namespace txtest {
    PayoutTestHelper::PayoutTestHelper(TestManager::pointer testManager) : TxHelper(testManager) {

    }

    TransactionFramePtr
    PayoutTestHelper::createPayoutTx(Account &source, AssetCode asset, BalanceID sourceBalanceID,
                                     uint64_t maxPayoutAmount, Fee &fee) {
        Operation op;
        op.body.type(OperationType::PAYOUT);

        PayoutOp& payoutOp = op.body.payoutOp();
        payoutOp.asset = asset;
        payoutOp.sourceBalanceID = sourceBalanceID;
        payoutOp.maxPayoutAmount = maxPayoutAmount;
        payoutOp.fee = fee;

        return TxHelper::txFromOperation(source, op, nullptr);
    }

    void
    PayoutTestHelper::applyPayoutTx(Account &source, AssetCode asset, BalanceID sourceBalanceID,
                                    uint64_t maxPayoutAmount, Fee &fee,
                                    PayoutResultCode expectedResult) {
        TransactionFramePtr txFrame;
        txFrame = createPayoutTx(source, asset, sourceBalanceID, maxPayoutAmount, fee);
        mTestManager->applyCheck(txFrame);
        REQUIRE(PayoutOpFrame::getInnerCode(
                txFrame->getResult().result.results()[0]) == expectedResult);
    }
}
}

