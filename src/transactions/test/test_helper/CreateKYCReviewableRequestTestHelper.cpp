// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include <transactions/CreateKYCReviewableRequestOpFrame.h>
#include "CreateKYCReviewableRequestTestHelper.h"
#include "ledger/ReviewableRequestHelper.h"
#include "ledger/AccountKYCHelper.h"
#include "ReviewChangeKYCRequestHelper.h"
#include "test/test_marshaler.h"
#include "CheckSaleStateTestHelper.h"
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


        UpdateKYCRequest
        CreateKYCRequestTestHelper::applyCreateChangeKYCRequest(Account &source, uint64_t requestID,
                                                                AccountType accountType,
                                                                longstring kycData, AccountID updatedAccount,
                                                                uint32 kycLevel,
                                                                CreateUpdateKYCRequestResultCode expectedResultCode) {

            TransactionFramePtr txFrame;

            auto accountHelper = AccountHelper::Instance();
            auto requestHelper = ReviewableRequestHelper::Instance();
            Database &db = mTestManager->getDB();
            auto accountBefore = accountHelper->loadAccount(updatedAccount, db);
            auto sourceAccount = accountHelper->loadAccount(source.key.getPublicKey(), db);
            txFrame = createKYCRequestTx(source, requestID, accountType, kycData, updatedAccount, kycLevel);
            std::vector<LedgerDelta::KeyEntryMap> stateBeforeOps;

            mTestManager->applyCheck(txFrame,stateBeforeOps);

            auto txResult = txFrame->getResult();
            auto actualResultCode =
                    CreateUpdateKYCRequestOpFrame::getInnerCode(txResult.result.results()[0]);

            REQUIRE(actualResultCode == expectedResultCode);

            auto txFee = mTestManager->getApp().getLedgerManager().getTxFee();
            REQUIRE(txResult.feeCharged == txFee);

            auto accountAfter = accountHelper->loadAccount(updatedAccount, db);

            auto opResult = txResult.result.results()[0].tr().createUpdateKYCRequestResult();

            if (actualResultCode != CreateUpdateKYCRequestResultCode::SUCCESS) {
                REQUIRE(accountAfter->getAccountType() == accountBefore->getAccountType());
                REQUIRE(accountAfter->getKYCLevel() == accountBefore->getKYCLevel());
                return opResult;
            }
            requestID = opResult.success().requestID;
            if (sourceAccount->getAccountType() == AccountType::MASTER) {
               return checkApprovedCreation(opResult,updatedAccount,stateBeforeOps[0]);
            }

            REQUIRE_FALSE(opResult.success().fulfilled);
            auto request = ReviewableRequestHelper::Instance()->loadRequest(requestID, db);
            REQUIRE(request);
            auto requestEntry = request->getRequestEntry();
            REQUIRE(request->getRequestor() == updatedAccount);

            auto referencePtr = getReference();

            REQUIRE(request->getReference() == referencePtr);
            REQUIRE(request->getRequestID() == requestID);
            REQUIRE(requestEntry.body.changeKYCRequest().updatedAccount == updatedAccount);
            REQUIRE(requestEntry.body.changeKYCRequest().kycLevel == kycLevel);
            REQUIRE(requestEntry.body.changeKYCRequest().kycData == kycData);
            REQUIRE(requestEntry.body.changeKYCRequest().accountTypeToSet == accountType);


            return opResult;
        }
        CreateKYCRequestResult
        CreateKYCRequestTestHelper::checkApprovedCreation(CreateKYCRequestResult opResult,AccountID updatedAccount,LedgerDelta::KeyEntryMap stateBeforeOp){
            REQUIRE(opResult.success().fulfilled);
            ReviewKYCRequestChecker kycRequestChecker(mTestManager);


            auto stateHelper = StateBeforeTxHelper(stateBeforeOp);
            auto requestFrame = stateHelper.getReviewableRequest(opResult.success().requestID);
            kycRequestChecker.checkApprove(requestFrame);

            auto request = ReviewableRequestHelper::Instance()->loadRequest(opResult.success().requestID, updatedAccount,
                                                                            ReviewableRequestType::CHANGE_KYC,
                                                                            mTestManager->getDB());
            REQUIRE_FALSE(request);
            return opResult;
        }


        TransactionFramePtr
        CreateKYCRequestTestHelper::createKYCRequestTx(Account &source, uint64_t requestID, AccountType accountType,
                                                       longstring kycData, AccountID updatedAccount,
                                                       uint32 kycLevel) {
            Operation baseOp;
            baseOp.body.type(OperationType::CREATE_KYC_REQUEST);
            auto &op = baseOp.body.createKYCRequestOp();
            op.changeKYCRequest.accountTypeToSet = accountType;
            op.requestID = requestID;
            op.changeKYCRequest.kycData = kycData;
            op.changeKYCRequest.kycLevel = kycLevel;
            op.changeKYCRequest.updatedAccount = updatedAccount;


            return txFromOperation(source, baseOp, nullptr);
        }
        xdr::pointer<string64> CreateKYCRequestTestHelper::getReference(){
            const auto hash = sha256(xdr::xdr_to_opaque(ReviewableRequestType::CHANGE_KYC));
            auto reference = binToHex(hash);
            const auto referencePtr = xdr::pointer<string64>(new string64(reference));
            return referencePtr;

        }
    }
}
