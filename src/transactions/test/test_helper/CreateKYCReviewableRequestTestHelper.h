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
                                                                    CreateKYCRequestResultCode expectedResultCode =CreateKYCRequestResultCode::SUCCESS);
            ;
            ReviewableRequestFrame::pointer CreateReviewableChangeKYCRequest(ChangeKYCRequest request,uint64 requestID);



        };

    }
}