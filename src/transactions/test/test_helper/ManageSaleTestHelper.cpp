#include <ledger/SaleHelper.h>
#include <ledger/ReviewableRequestHelper.h>
#include <lib/catch.hpp>
#include <transactions/dex/ManageSaleOpFrame.h>
#include <ledger/SaleAnteHelper.h>
#include "ManageAssetTestHelper.h"
#include "ManageSaleTestHelper.h"
#include "TestManager.h"
#include "TxHelper.h"
#include "StateBeforeTxHelper.h"
#include "CheckSaleStateTestHelper.h"

class pointer;
namespace stellar {
    namespace txtest {

        txtest::ManageSaleTestHelper::ManageSaleTestHelper(TestManager::pointer testManager) : TxHelper(testManager) {

        }

        ManageSaleOp::_data_t ManageSaleTestHelper::createDataForAction(ManageSaleAction action, uint64_t *requestID,
                                                                        std::string *newDetails) {
            ManageSaleOp::_data_t data;

            switch (action) {
                case ManageSaleAction::CREATE_UPDATE_DETAILS_REQUEST: {
                    if (!requestID || !newDetails) {
                        throw std::runtime_error("Request ID and new details string cannot be nullptr "
                                                 "while creating update sale details request");
                    }
                    data.action(ManageSaleAction::CREATE_UPDATE_DETAILS_REQUEST);
                    data.updateSaleDetailsData().requestID = *requestID;
                    data.updateSaleDetailsData().newDetails = *newDetails;
                    break;
                }
                case ManageSaleAction::CANCEL: {
                    data.action(ManageSaleAction::CANCEL);
                    break;
                }
                default: {
                    throw std::runtime_error("Unexpected ManageSaleAction type");
                }
            }

            return data;
        }

        ManageSaleOp::_data_t ManageSaleTestHelper::setSaleState(SaleState saleState)
        {
            ManageSaleOp::_data_t data;
            data.action(ManageSaleAction::SET_STATE);
            data.saleState() = saleState;
            return data;
        }

        ManageSaleOp::_data_t ManageSaleTestHelper::createUpdateSaleEndTimeRequest(uint64_t requestID,
                                                                                   uint64_t newEndTime) {
            ManageSaleOp::_data_t data;
            data.action(ManageSaleAction::CREATE_UPDATE_END_TIME_REQUEST);
            data.updateSaleEndTimeData().requestID = requestID;
            data.updateSaleEndTimeData().newEndTime = newEndTime;
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
            auto saleHelper = SaleHelper::Instance();

            auto saleBeforeOp = saleHelper->loadSale(saleID, db);

            ReviewableRequestFrame::pointer requestBeforeTx;

            switch(data.action()) {
                case ManageSaleAction::CREATE_UPDATE_DETAILS_REQUEST: {
                    requestBeforeTx = reviewableRequestHelper->loadRequest(data.updateSaleDetailsData().requestID, db);
                    break;
                }
                case ManageSaleAction::CREATE_UPDATE_END_TIME_REQUEST: {
                    requestBeforeTx = reviewableRequestHelper->loadRequest(data.updateSaleEndTimeData().requestID, db);
                    break;
                }
                default:
                    break;
            }

            auto saleAntesBeforeTx = SaleAnteHelper::Instance()->loadSaleAntes(saleID, db);

            std::vector<LedgerDelta::KeyEntryMap> stateBeforeOp;
            TransactionFramePtr txFrame;
            txFrame = createManageSaleTx(source, saleID, data);
            mTestManager->applyCheck(txFrame, stateBeforeOp);

            auto txResult = txFrame->getResult();
            auto actualResultCode = ManageSaleOpFrame::getInnerCode(txResult.result.results()[0]);

            REQUIRE(actualResultCode == expectedResultCode);

            auto txFee = mTestManager->getApp().getLedgerManager().getTxFee();
            REQUIRE(txResult.feeCharged == txFee);

            ManageSaleResult manageSaleResult = txResult.result.results()[0].tr().manageSaleResult();

            if (actualResultCode != ManageSaleResultCode::SUCCESS)
                return manageSaleResult;

            auto saleAfterOp = saleHelper->loadSale(saleID, db);

            switch (data.action()) {
                case ManageSaleAction::CREATE_UPDATE_DETAILS_REQUEST: {
                    auto requestAfterTx = reviewableRequestHelper->loadRequest(
                            manageSaleResult.success().response.requestID(), db);
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

                    break;
                }
                case ManageSaleAction::CANCEL: {
                    REQUIRE(!saleAfterOp);
                    REQUIRE(stateBeforeOp.size() == 1);
                    StateBeforeTxHelper stateBeforeTxHelper(stateBeforeOp[0]);
                    CheckSaleStateHelper(mTestManager).ensureCancel(saleID, stateBeforeTxHelper, saleAntesBeforeTx);
                    break;
                }
                case ManageSaleAction::SET_STATE: {
                    REQUIRE(!!saleAfterOp);
                    auto currentState = saleAfterOp->getState();
                    auto expectedState = data.saleState();
                    REQUIRE(currentState == expectedState);
                    break;
                }
                case ManageSaleAction::CREATE_UPDATE_END_TIME_REQUEST: {
                    auto requestAfterTx = reviewableRequestHelper->loadRequest(
                            manageSaleResult.success().response.updateEndTimeRequestID(), db);
                    REQUIRE(!!requestAfterTx);

                    auto requestAfterTxEntry = requestAfterTx->getRequestEntry();

                    REQUIRE(requestAfterTxEntry.body.updateSaleEndTimeRequest().saleID == saleID);
                    REQUIRE(requestAfterTxEntry.body.updateSaleEndTimeRequest().newEndTime ==
                            data.updateSaleEndTimeData().newEndTime);

                    if (!!requestBeforeTx) {
                        auto requestBeforeTxEntry = requestBeforeTx->getRequestEntry();

                        REQUIRE(requestBeforeTxEntry.body.updateSaleEndTimeRequest().saleID ==
                                requestAfterTxEntry.body.updateSaleEndTimeRequest().saleID);

                        REQUIRE(requestBeforeTxEntry.body.updateSaleEndTimeRequest().newEndTime !=
                                requestAfterTxEntry.body.updateSaleEndTimeRequest().newEndTime);
                    }

                    break;
                }
            }

            return manageSaleResult;
        }
    }
}