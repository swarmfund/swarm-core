#include <ledger/BalanceHelperLegacy.h>
#include <ledger/FeeHelper.h>
#include <transactions/payment/PaymentOpV2Frame.h>
#include <iterator>
#include "PaymentV2TestHelper.h"
#include "test/test_marshaler.h"

namespace stellar {
    namespace txtest {
        PaymentV2TestHelper::PaymentV2TestHelper(TestManager::pointer testManager) : TxHelper(testManager) {
        }

        PaymentOpV2::_destination_t PaymentV2TestHelper::createDestinationForAccount(AccountID destAccountID) {
            PaymentOpV2::_destination_t destination;
            destination.type(PaymentDestinationType::ACCOUNT);
            destination.accountID() = destAccountID;
            return destination;
        }

        PaymentOpV2::_destination_t PaymentV2TestHelper::createDestinationForBalance(BalanceID destBalanceID) {
            PaymentOpV2::_destination_t destination;
            destination.type(PaymentDestinationType::BALANCE);
            destination.balanceID() = destBalanceID;
            return destination;
        }

        FeeDataV2 PaymentV2TestHelper::createFeeData(uint64 fixedFee, uint64 paymentFee, AssetCode feeAsset) {
            FeeDataV2 feeData;

            feeData.fixedFee = fixedFee;
            feeData.maxPaymentFee = paymentFee;
            feeData.feeAsset = feeAsset;

            return feeData;
        }

        PaymentFeeDataV2 PaymentV2TestHelper::createPaymentFeeData(FeeDataV2 sourceFeeData, FeeDataV2 destFeeData,
                                                                   bool sourcePaysForDest) {
            PaymentFeeDataV2 paymentFeeData;

            paymentFeeData.sourceFee = sourceFeeData;
            paymentFeeData.destinationFee = destFeeData;
            paymentFeeData.sourcePaysForDest = sourcePaysForDest;

            return paymentFeeData;
        }

        TransactionFramePtr PaymentV2TestHelper::createPaymentV2Tx(Account &source, BalanceID sourceBalanceID,
                                                                   PaymentOpV2::_destination_t destination,
                                                                   uint64_t amount, PaymentFeeDataV2 feeData,
                                                                   std::string subject, std::string reference) {
            Operation baseOp;
            baseOp.body.type(OperationType::PAYMENT_V2);
            auto &op = baseOp.body.paymentOpV2();
            op.sourceBalanceID = sourceBalanceID;
            op.destination = destination;
            op.amount = amount;
            op.feeData = feeData;
            op.subject = subject;
            op.reference = reference;

            return txFromOperation(source, baseOp, nullptr);
        }

        PaymentV2Result PaymentV2TestHelper::applyPaymentV2Tx(Account &source, BalanceID sourceBalanceID,
                                                              PaymentOpV2::_destination_t destination, uint64_t amount,
                                                              PaymentFeeDataV2 feeData, std::string subject,
                                                              std::string reference, PaymentV2Delta *paymentDelta,
                                                              PaymentV2ResultCode expectedResultCode) {
            auto &db = mTestManager->getDB();
            auto balanceHelper = BalanceHelperLegacy::Instance();

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

            TransactionFramePtr txFrame;
            txFrame = createPaymentV2Tx(source, sourceBalanceID, destination, amount, feeData, subject, reference);
            mTestManager->applyCheck(txFrame);

            auto txResult = txFrame->getResult();
            auto actualResultCode = PaymentOpV2Frame::getInnerCode(txResult.result.results()[0]);

            REQUIRE(actualResultCode == expectedResultCode);

            auto txFee = mTestManager->getApp().getLedgerManager().getTxFee();
            REQUIRE(txResult.feeCharged == txFee);

            auto opResult = txResult.result.results()[0].tr().paymentV2Result();

            if (!paymentDelta)
                return opResult;

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

            return opResult;
        }
    }
}

