// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include <transactions/CreateKYCReviewableRequestOpFrame.h>
#include "CreateKYCReviewableRequestTestHelper.h"
#include "ledger/ReviewableRequestHelper.h"
#include "ledger/AccountKYCHelper.h"

#include "test/test_marshaler.h"

#include "ledger/AccountHelper.h"
#include "bucket/BucketApplicator.h"


namespace stellar
{
    namespace txtest
    {





     CreateKYCRequestTestHelper::CreateKYCRequestTestHelper(const TestManager::pointer testManager) : TxHelper(testManager)
        {
        }
        ReviewableRequestFrame::pointer CreateKYCRequestTestHelper::CreateReviewableChangeKYCRequest(ChangeKYCRequest request,uint64 requestID){
            const auto hash = sha256(xdr::xdr_to_opaque(ReviewableRequestType::CHANGE_KYC));
            auto reference = binToHex(hash);
            const auto referencePtr = xdr::pointer<string64>(new string64(reference));

            auto frame = ReviewableRequestFrame::createNew(requestID,request.updatedAccount, mTestManager->getApp().getMasterID(),
                                                           referencePtr,mTestManager->getLedgerManager().getCloseTime());
            frame->mEntry.data.reviewableRequest().body.changeKYCRequest() = request;
            frame->recalculateHashRejectReason();
            return frame;
        }



        CreateKYCRequestResult
        CreateKYCRequestTestHelper::applyCreateChangeKYCRequest(Account &source, uint64_t requestID,AccountType accountType,
                                                                                 longstring kycData,AccountID updatedAccount,
                                                                                 uint32 kycLevel,
                                                                                 CreateKYCRequestResultCode expectedResultCode)
        {

            TransactionFramePtr txFrame;

            auto accountHelper = AccountHelper::Instance();
            auto requestHelper = ReviewableRequestHelper::Instance();
            Database &db = mTestManager->getDB();
            auto accountBefore = accountHelper->loadAccount(updatedAccount, db);
            auto sourceAccount = accountHelper->loadAccount(source.key.getPublicKey(),db);
            txFrame = createKYCRequestTx(source, requestID, accountType, kycData, updatedAccount, kycLevel);

            mTestManager->applyCheck(txFrame);

            auto txResult = txFrame->getResult();
            auto actualResultCode =
                    CreateKYCRequestOpFrame::getInnerCode(txResult.result.results()[0]);

            REQUIRE(actualResultCode == expectedResultCode);

            auto txFee = mTestManager->getApp().getLedgerManager().getTxFee();
            REQUIRE(txResult.feeCharged == txFee);

            auto accountAfter = accountHelper->loadAccount(updatedAccount, db);

            auto opResult = txResult.result.results()[0].tr().createKYCRequestResult();
            if(actualResultCode != CreateKYCRequestResultCode::SUCCESS){

                auto request = ReviewableRequestHelper::Instance()->loadRequest(requestID,db);
                REQUIRE_FALSE(request);
                REQUIRE(accountAfter->getAccountType() == accountBefore->getAccountType());
                REQUIRE(accountAfter->getKYCLevel() == accountBefore->getKYCLevel());
                auto accountKYC = AccountKYCHelper::Instance()->loadAccountKYC(updatedAccount, db);
                REQUIRE_FALSE(accountKYC);
            }else {
                requestID= opResult.success().requestID;

                if (sourceAccount->getAccountType() == AccountType::MASTER) {
                    if(accountAfter->mEntry.ext.v() == LedgerVersion::USE_KYC_LEVEL) {
                        REQUIRE(accountAfter->getKYCLevel() == kycLevel);
                    }
                    REQUIRE(accountAfter->getAccountType() == accountType);
                    auto accountKYC = AccountKYCHelper::Instance()->loadAccountKYC(updatedAccount, db);
                    REQUIRE(accountKYC);
                    REQUIRE(accountKYC->getKYCData() == kycData);
                    auto request = ReviewableRequestHelper::Instance()->loadRequest(requestID,updatedAccount,ReviewableRequestType::CHANGE_KYC,mTestManager->getDB());
                    REQUIRE_FALSE(request);
                }else{
                    auto request = ReviewableRequestHelper::Instance()->loadRequest(requestID,db);
                    REQUIRE(request);
                    auto requestEntry =request->getRequestEntry();
                    REQUIRE(request->getRequestor() == updatedAccount);

                    const auto hash = sha256(xdr::xdr_to_opaque(ReviewableRequestType::CHANGE_KYC));
                    auto reference = binToHex(hash);
                    const auto referencePtr = xdr::pointer<string64>(new string64(reference));

                    REQUIRE(request->getReference() == referencePtr);
                    REQUIRE(request->getRequestID() == requestID);
                    REQUIRE(requestEntry.body.changeKYCRequest().updatedAccount == updatedAccount);
                    REQUIRE(requestEntry.body.changeKYCRequest().kycLevel == kycLevel);
                    REQUIRE(requestEntry.body.changeKYCRequest().kycData == kycData);
                    REQUIRE(requestEntry.body.changeKYCRequest().accountTypeToSet == accountType);
                }

            }

            return opResult;
        }

        TransactionFramePtr CreateKYCRequestTestHelper::createKYCRequestTx(Account &source, uint64_t requestID,AccountType accountType,
                                                                           longstring kycData,AccountID updatedAccount,
                                                                           uint32 kycLevel)
        {
            Operation baseOp;
            baseOp.body.type(OperationType::CREATE_KYC_REQUEST);
            auto& op = baseOp.body.createKYCRequestOp();
            op.changeKYCRequest.accountTypeToSet = accountType;
            op.requestID = requestID;
            op.changeKYCRequest.kycData= kycData;
            op.changeKYCRequest.kycLevel = kycLevel;
            op.changeKYCRequest.updatedAccount = updatedAccount;


            return txFromOperation(source, baseOp, nullptr);
        }
    }
}
