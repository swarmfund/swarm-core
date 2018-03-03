#pragma once
#include "TxHelper.h"
#include "ledger/ReviewableRequestFrame.h"
namespace stellar {
    namespace txtest {


        class CreateKYCRequestTestHelper : TxHelper{
        public:
            explicit CreateKYCRequestTestHelper(TestManager::pointer testManager);

            TransactionFramePtr createKYCRequestTx(Account &source, uint64_t requestID,AccountType accountType,
                                                                               longstring kycData,AccountID updatedAccount,
                                                                               uint32 kycLevel = 0 );


            CreateKYCRequestResult
            applyCreateChangeKYCRequest(Account &source, uint64_t requestID,AccountType accountType,
                                                                    longstring kycData,AccountID updatedAccount,
                                                                    uint32 kycLevel,
                                                                    CreateKYCRequestResultCode expectedResultCode);
        protected:
            CreateKYCRequestResult
            checkApprovedCreation(CreateKYCRequestResult opResult,AccountID updatedAccount,LedgerDelta::KeyEntryMap stateBeforeOp);

                ReviewableRequestFrame::pointer createReviewableChangeKYCRequest(ChangeKYCRequest request,uint64 requestID);
            xdr::pointer<string64> getReference();



        };

    }
}