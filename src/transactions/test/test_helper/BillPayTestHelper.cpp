
#include <ledger/BalanceHelper.h>
#include <transactions/payment/BillPayOpFrame.h>
#include <lib/catch.hpp>
#include "BillPayTestHelper.h"


namespace stellar
{
namespace txtest
{
    BillPayTestHelper::BillPayTestHelper(TestManager::pointer testManager) : TxHelper(testManager)
    {
    }

    TransactionFramePtr
    BillPayTestHelper::createBillPayTx(Account &source, uint64_t requestID, BalanceID sourceBalanceID,
                                       PaymentOpV2::_destination_t destination,
                                       uint64_t amount, PaymentFeeDataV2 feeData,
                                       std::string subject, std::string reference)
    {
        Operation baseOp;
        baseOp.body.type(OperationType::BILL_PAY);
        auto &op = baseOp.body.billPayOp();
        op.requestID = requestID;
        op.paymentDetails.sourceBalanceID = sourceBalanceID;
        op.paymentDetails.destination = destination;
        op.paymentDetails.amount = amount;
        op.paymentDetails.feeData = feeData;
        op.paymentDetails.subject = subject;
        op.paymentDetails.reference = reference;

        return txFromOperation(source, baseOp, nullptr);
    }

    BillPayResult
    BillPayTestHelper::applyBillPayTx(Account &source, uint64_t requestID, BalanceID sourceBalanceID,
                                      PaymentOpV2::_destination_t destination, uint64_t amount,
                                      PaymentFeeDataV2 feeData, std::string subject,
                                      std::string reference, PaymentV2Delta *paymentDelta,
                                      BillPayResultCode expectedResultCode)
    {
        auto &db = mTestManager->getDB();
        auto balanceHelper = BalanceHelper::Instance();

        auto sourceBalanceBeforeTx = balanceHelper->loadBalance(sourceBalanceID, db);

        // for now destination fee can be charged only in asset which is the same as asset of payment amount
        // so it's ok to assert that sourceBalance asset is the same as destination fee asset
        bool isCrossAssetPayment = feeData.destinationFee.feeAsset != feeData.sourceFee.feeAsset;

        BalanceFrame::pointer sourceFeeBalanceBeforeTx;
        if (!!sourceBalanceBeforeTx && isCrossAssetPayment)
            sourceFeeBalanceBeforeTx = balanceHelper->loadBalance(sourceBalanceBeforeTx->getAccountID(),
                                                                  feeData.sourceFee.feeAsset, db, nullptr);

        BalanceFrame::pointer destBalanceBeforeTx;
        if (destination.type() == PaymentDestinationType::BALANCE) {
            destBalanceBeforeTx = balanceHelper->loadBalance(destination.balanceID(), db);
        }

        auto commissionBalancesBeforeTx = balanceHelper->loadBalances(mTestManager->getApp().getCommissionID(), db);

        auto txFrame = createBillPayTx(source, requestID, sourceBalanceID, destination, amount, feeData, subject,
                reference);
        mTestManager->applyCheck(txFrame);

        auto txResult = txFrame->getResult();
        auto opResult = txResult.result.results()[0];
        auto actualResultCode = BillPayOpFrame::getInnerCode(opResult);

        REQUIRE(actualResultCode == expectedResultCode);

        auto txFee = mTestManager->getApp().getLedgerManager().getTxFee();
        REQUIRE(txResult.feeCharged == txFee);

        if (!paymentDelta)
            return opResult.tr().billPayResult();

        REQUIRE(paymentDelta->source.size() < 3);
        REQUIRE(paymentDelta->destination.size() < 2);
        REQUIRE(paymentDelta->commission.size() < 3);

        auto sourceDelta = paymentDelta->source;
        auto destDelta = paymentDelta->destination;
        auto commissionDelta = paymentDelta->commission;

        for (auto &item : sourceDelta) {
            if (item.asset == sourceBalanceBeforeTx->getAsset()) {
                auto sourceBalanceAfterTx = balanceHelper->loadBalance(sourceBalanceID, db);
                REQUIRE(sourceBalanceAfterTx->getAmount() == sourceBalanceBeforeTx->getAmount() + item.amountDelta);
                continue;
            }

            REQUIRE(isCrossAssetPayment);

            if (item.asset == sourceFeeBalanceBeforeTx->getAsset()) {
                auto sourceFeeBalanceAfterTx = balanceHelper->loadBalance(sourceFeeBalanceBeforeTx->getBalanceID(),
                                                                          db);
                REQUIRE(sourceFeeBalanceAfterTx->getAmount() == sourceFeeBalanceBeforeTx->getAmount() +
                                                                item.amountDelta);
                continue;
            }

            throw std::runtime_error("Unexpected asset code");
        }

        for (auto &item : destDelta) {
            BalanceFrame::pointer destBalanceAfterTx;

            switch (destination.type()) {
                case PaymentDestinationType::ACCOUNT: {
                    destBalanceAfterTx = balanceHelper->loadBalance(destination.accountID(), item.asset, db,
                                                                    nullptr);
                    break;
                }
                case PaymentDestinationType::BALANCE: {
                    destBalanceAfterTx = balanceHelper->loadBalance(destination.balanceID(), db);
                    break;
                }
            }

            REQUIRE(!!destBalanceAfterTx);

            if (!destBalanceBeforeTx) {
                REQUIRE(destBalanceAfterTx->getAmount() == item.amountDelta);
                continue;
            }

            REQUIRE(destBalanceAfterTx->getAmount() == destBalanceBeforeTx->getAmount() + item.amountDelta);
        }

        for (auto &item : commissionDelta) {
            BalanceFrame::pointer commissionBalanceBeforeTx;

            if (commissionBalancesBeforeTx.count(item.asset) > 0)
                commissionBalanceBeforeTx = commissionBalancesBeforeTx[item.asset];

            auto commissionBalanceAfterTx = balanceHelper->loadBalance(mTestManager->getApp().getCommissionID(),
                                                                       item.asset, db, nullptr);
            if (!commissionBalanceBeforeTx) {
                REQUIRE(commissionBalanceAfterTx->getAmount() == item.amountDelta);
                continue;
            }

            REQUIRE(commissionBalanceAfterTx->getAmount() == commissionBalanceBeforeTx->getAmount() +
                                                             item.amountDelta);
        }

        return opResult.tr().billPayResult();
    }
}
}