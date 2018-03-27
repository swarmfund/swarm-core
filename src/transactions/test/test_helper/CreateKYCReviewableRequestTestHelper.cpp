// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include <transactions/CreateKYCReviewableRequestOpFrame.h>
#include "CreateKYCReviewableRequestTestHelper.h"
#include "ledger/ReviewableRequestHelper.h"
#include "ledger/AccountKYCHelper.h"
#include "ReviewUpdateKYCRequestHelper.h"
#include "test/test_marshaler.h"
#include "ReviewRequestTestHelper.h"
#include "ledger/AccountHelper.h"
#include "bucket/BucketApplicator.h"


namespace stellar {
    namespace txtest {
        CreateKYCRequestTestHelper::CreateKYCRequestTestHelper(const TestManager::pointer testManager) : TxHelper(
                testManager) {
        }

        ReviewableRequestFrame::pointer
        CreateKYCRequestTestHelper::createReviewableChangeKYCRequest(UpdateKYCRequest request, uint64 requestID) {
            auto referencePtr = getReference();
            auto frame = ReviewableRequestFrame::createNew(requestID, request.accountToUpdateKYC,
                                                           mTestManager->getApp().getMasterID(),
                                                           referencePtr,
                                                           mTestManager->getLedgerManager().getCloseTime());
            frame->mEntry.data.reviewableRequest().body.updateKYCRequest() = request;
            frame->recalculateHashRejectReason();
            return frame;
        }

        CreateUpdateKYCRequestResult
        CreateKYCRequestTestHelper::applyCreateUpdateKYCRequest(Account &source, uint64_t requestID,
                                                                AccountID accountToUpdateKYC, AccountType accountType,
                                                                longstring kycData, uint32 kycLevel, uint32 *allTasks,
                                                                CreateUpdateKYCRequestResultCode expectedResultCode) {
            auto accountHelper = AccountHelper::Instance();
            auto requestHelper = ReviewableRequestHelper::Instance();

            Database &db = mTestManager->getDB();
            auto accountBefore = accountHelper->loadAccount(accountToUpdateKYC, db);
            auto sourceAccount = accountHelper->loadAccount(source.key.getPublicKey(), db);

            ReviewableRequestFrame::pointer requestBeforeTx;

            if (requestID != 0) {
                requestBeforeTx = requestHelper->loadRequest(requestID, db);
            }

            TransactionFramePtr txFrame;
            txFrame = createUpdateKYCRequestTx(source, requestID, accountToUpdateKYC, accountType, kycData, kycLevel,
                                               allTasks);

            std::vector<LedgerDelta::KeyEntryMap> stateBeforeOps;
            mTestManager->applyCheck(txFrame, stateBeforeOps);

            auto txResult = txFrame->getResult();
            auto actualResultCode = CreateUpdateKYCRequestOpFrame::getInnerCode(txResult.result.results()[0]);

            REQUIRE(actualResultCode == expectedResultCode);

            auto txFee = mTestManager->getApp().getLedgerManager().getTxFee();
            REQUIRE(txResult.feeCharged == txFee);

            auto accountAfter = accountHelper->loadAccount(accountToUpdateKYC, db);

            auto opResult = txResult.result.results()[0].tr().createUpdateKYCRequestResult();

            if (actualResultCode != CreateUpdateKYCRequestResultCode::SUCCESS) {
                REQUIRE(accountAfter->getAccountType() == accountBefore->getAccountType());
                REQUIRE(accountAfter->getKYCLevel() == accountBefore->getKYCLevel());

                if (requestBeforeTx) {
                    auto requestAfterTx = ReviewableRequestHelper::Instance()->loadRequest(requestID, db);
                    auto requestAfterTxEntry = requestAfterTx->getRequestEntry();
                    auto requestBeforeTxEntry = requestBeforeTx->getRequestEntry();

                    REQUIRE(requestAfterTxEntry.body.updateKYCRequest().accountToUpdateKYC ==
                            requestBeforeTxEntry.body.updateKYCRequest().accountToUpdateKYC);
                    REQUIRE(requestAfterTxEntry.body.updateKYCRequest().accountTypeToSet ==
                            requestBeforeTxEntry.body.updateKYCRequest().accountTypeToSet);
                    REQUIRE(requestAfterTxEntry.body.updateKYCRequest().kycData ==
                            requestBeforeTxEntry.body.updateKYCRequest().kycData);
                    REQUIRE(requestAfterTxEntry.body.updateKYCRequest().kycLevel ==
                            requestBeforeTxEntry.body.updateKYCRequest().kycLevel);
                    REQUIRE(requestAfterTxEntry.body.updateKYCRequest().allTasks ==
                            requestBeforeTxEntry.body.updateKYCRequest().allTasks);
                    REQUIRE(requestAfterTxEntry.body.updateKYCRequest().pendingTasks ==
                            requestBeforeTxEntry.body.updateKYCRequest().pendingTasks);
                    REQUIRE(requestAfterTxEntry.body.updateKYCRequest().sequenceNumber ==
                            requestBeforeTxEntry.body.updateKYCRequest().sequenceNumber);
                    REQUIRE(requestAfterTxEntry.body.updateKYCRequest().externalDetails ==
                            requestBeforeTxEntry.body.updateKYCRequest().externalDetails);
                }

                return opResult;
            }

            requestID = opResult.success().requestID;

            if (sourceAccount->getAccountType() == AccountType::MASTER && allTasks && *allTasks == 0) {
                return checkApprovedCreation(opResult, accountToUpdateKYC, stateBeforeOps[0]);
            }

            REQUIRE_FALSE(opResult.success().fulfilled);

            auto requestAfterTx = ReviewableRequestHelper::Instance()->loadRequest(requestID, db);
            REQUIRE(requestAfterTx);

            auto requestAfterTxEntry = requestAfterTx->getRequestEntry();
            auto referencePtr = getReference();

            REQUIRE(requestAfterTx->getReference() == referencePtr);
            REQUIRE(requestAfterTx->getRequestID() == requestID);
            REQUIRE(requestAfterTxEntry.body.updateKYCRequest().accountToUpdateKYC == accountToUpdateKYC);
            REQUIRE(requestAfterTxEntry.body.updateKYCRequest().accountTypeToSet == accountType);
            REQUIRE(requestAfterTxEntry.body.updateKYCRequest().kycLevel == kycLevel);
            REQUIRE(requestAfterTxEntry.body.updateKYCRequest().kycData == kycData);

            if (allTasks) {
                REQUIRE(requestAfterTxEntry.body.updateKYCRequest().allTasks == *allTasks);
            } else {
                REQUIRE(requestAfterTxEntry.body.updateKYCRequest().allTasks ==
                        CreateUpdateKYCRequestOpFrame::defaultTasks);
            }

            REQUIRE(requestAfterTxEntry.body.updateKYCRequest().pendingTasks ==
                    requestAfterTxEntry.body.updateKYCRequest().allTasks);

            if (requestBeforeTx) {
                auto requestBeforeTxEntry = requestBeforeTx->getRequestEntry();
                REQUIRE(requestAfterTxEntry.body.updateKYCRequest().sequenceNumber ==
                        requestBeforeTxEntry.body.updateKYCRequest().sequenceNumber);
                REQUIRE(requestAfterTxEntry.body.updateKYCRequest().externalDetails ==
                        requestBeforeTxEntry.body.updateKYCRequest().externalDetails);
            } else {
                REQUIRE(requestAfterTxEntry.body.updateKYCRequest().sequenceNumber == 0);
            }

            return opResult;
        }

        TransactionFramePtr
        CreateKYCRequestTestHelper::createUpdateKYCRequestTx(Account &source, uint64_t requestID,
                                                             AccountID accountToUpdateKYC, AccountType accountType,
                                                             longstring kycData, uint32 kycLevel, uint32 *allTasks) {
            Operation baseOp;
            baseOp.body.type(OperationType::CREATE_KYC_REQUEST);
            auto &op = baseOp.body.createUpdateKYCRequestOp();
            op.requestID = requestID;
            op.updateKYCRequestData.accountToUpdateKYC = accountToUpdateKYC;
            op.updateKYCRequestData.accountTypeToSet = accountType;
            op.updateKYCRequestData.kycData = kycData;
            op.updateKYCRequestData.kycLevel = kycLevel;
            if (allTasks) {
                op.updateKYCRequestData.allTasks.activate() = *allTasks;
            }

            return txFromOperation(source, baseOp, nullptr);
        }

        CreateUpdateKYCRequestResult
        CreateKYCRequestTestHelper::checkApprovedCreation(CreateUpdateKYCRequestResult opResult,
                                                          AccountID accountToUpdateKYC,
                                                          LedgerDelta::KeyEntryMap stateBeforeOp) {
            REQUIRE(opResult.success().fulfilled);

            auto stateHelper = StateBeforeTxHelper(stateBeforeOp);
            auto requestFrame = stateHelper.getReviewableRequest(opResult.success().requestID);

            ReviewKYCRequestChecker kycRequestChecker(mTestManager);
            kycRequestChecker.checkApprove(requestFrame);

            auto request = ReviewableRequestHelper::Instance()->loadRequest(opResult.success().requestID,
                                                                            accountToUpdateKYC,
                                                                            ReviewableRequestType::UPDATE_KYC,
                                                                            mTestManager->getDB());
            REQUIRE_FALSE(request);

            return opResult;
        }

        xdr::pointer<string64> CreateKYCRequestTestHelper::getReference() {
            const auto hash = sha256(xdr::xdr_to_opaque(ReviewableRequestType::UPDATE_KYC));
            auto reference = binToHex(hash);
            const auto referencePtr = xdr::pointer<string64>(new string64(reference));
            return referencePtr;
        }
    }
}
