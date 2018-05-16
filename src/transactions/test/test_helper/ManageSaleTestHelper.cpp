#include <ledger/SaleHelper.h>
#include <ledger/ReviewableRequestHelper.h>
#include <lib/catch.hpp>
#include <transactions/ManageSaleOpFrame.h>
#include "ManageAssetTestHelper.h"
#include "ManageSaleTestHelper.h"
#include "TestManager.h"
#include "TxHelper.h"

class pointer;
namespace stellar {
    namespace txtest {

        txtest::ManageSaleTestHelper::ManageSaleTestHelper(TestManager::pointer testManager) : TxHelper(testManager) {

        }

        ManageSaleOp::_data_t
        txtest::ManageSaleTestHelper::createDataForUpdateSaleDetails(uint64_t requestID, std::string newDetails) {
            ManageSaleOp::_data_t data;
            data.action(ManageSaleAction::CREATE_UPDATE_DETAILS_REQUEST);
            data.updateSaleDetailsData().requestID = requestID;
            data.updateSaleDetailsData().newDetails = newDetails;
            return data;
        }

        TransactionFramePtr
        ManageSaleTestHelper::createManageSaleTx(Account &source, uint64_t saleID, ManageSaleOp::_data_t data) {
            Operation op;
            op.body.type(OperationType::MANAGE_SALE);
            ManageSaleOp &manageSaleOp = op.body.manageSaleOp();
            manageSaleOp.saleID = saleID;
            manageSaleOp.data = data;
            return txFromOperation(source, op, nullptr);
        }

        ManageSaleResult
        ManageSaleTestHelper::applyManageSaleTx(Account &source, uint64_t saleID, ManageSaleOp::_data_t data,
                                                ManageSaleResultCode expectedResultCode) {
            auto &db = mTestManager->getDB();
            auto reviewableRequestHelper = ReviewableRequestHelper::Instance();

            ReviewableRequestFrame::pointer requestBeforeTx;
            if (data.action() == ManageSaleAction::CREATE_UPDATE_DETAILS_REQUEST) {
                requestBeforeTx = reviewableRequestHelper->loadRequest(data.updateSaleDetailsData().requestID, db);
            }

            TransactionFramePtr txFrame;
            txFrame = createManageSaleTx(source, saleID, data);
            mTestManager->applyCheck(txFrame);

            auto txResult = txFrame->getResult();
            auto actualResultCode = ManageSaleOpFrame::getInnerCode(txResult.result.results()[0]);

            REQUIRE(actualResultCode == expectedResultCode);

            auto txFee = mTestManager->getApp().getLedgerManager().getTxFee();
            REQUIRE(txResult.feeCharged == txFee);

            ManageSaleResult manageSaleResult = txResult.result.results()[0].tr().manageSaleResult();

            if (actualResultCode != ManageSaleResultCode::SUCCESS)
                return manageSaleResult;

            auto requestAfterTx = reviewableRequestHelper->loadRequest(manageSaleResult.success().response.requestID(),
                                                                       db);
            REQUIRE(!!requestAfterTx);

            auto requestAfterTxEntry = requestAfterTx->getRequestEntry();

            REQUIRE(requestAfterTxEntry.body.updateSaleDetailsRequest().saleID == saleID);
            REQUIRE(requestAfterTxEntry.body.updateSaleDetailsRequest().newDetails ==
                    data.updateSaleDetailsData().newDetails);

            if (!!requestBeforeTx) {
                auto requestBeforeTxEntry = requestBeforeTx->getRequestEntry();

                REQUIRE(requestBeforeTxEntry.body.updateSaleDetailsRequest().saleID ==
                        requestAfterTxEntry.body.updateSaleDetailsRequest().saleID);

                REQUIRE(requestBeforeTxEntry.body.updateSaleDetailsRequest().newDetails !=
                        requestAfterTxEntry.body.updateSaleDetailsRequest().newDetails);
            }

            return manageSaleResult;
        }
    }
}