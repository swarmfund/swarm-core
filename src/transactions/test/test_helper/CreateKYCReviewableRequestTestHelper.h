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


            CreateUpdateKYCRequestResult
            applyCreateChangeKYCRequest(Account &source, uint64_t requestID,AccountType accountType,
                                                                    longstring kycData,AccountID updatedAccount,
                                                                    uint32 kycLevel,
                                                                    CreateUpdateKYCRequestResultCode expectedResultCode);
        protected:
            CreateUpdateKYCRequestResult
            checkApprovedCreation(CreateUpdateKYCRequestResult opResult,AccountID updatedAccount,LedgerDelta::KeyEntryMap stateBeforeOp);

                ReviewableRequestFrame::pointer createReviewableChangeKYCRequest(UpdateKYCRequest request,uint64 requestID);
            xdr::pointer<string64> getReference();



        };

    }
}