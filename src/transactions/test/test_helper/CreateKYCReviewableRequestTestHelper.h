#pragma once

#include "TxHelper.h"
#include "ledger/ReviewableRequestFrame.h"

namespace stellar {
    namespace txtest {
        class CreateKYCRequestTestHelper : TxHelper {
        public:
            explicit CreateKYCRequestTestHelper(TestManager::pointer testManager);

            TransactionFramePtr createUpdateKYCRequestTx(Account &source, uint64_t requestID,
                                                         AccountID accountToUpdateKYC, AccountType accountType,
                                                         longstring kycData, uint32 kycLevel, uint32 *allTasks);


            CreateUpdateKYCRequestResult
            applyCreateUpdateKYCRequest(Account &source, uint64_t requestID, AccountID accountToUpdateKYC,
                                        AccountType accountType, longstring kycData, uint32 kycLevel,
                                        uint32 *allTasks = nullptr,
                                        CreateUpdateKYCRequestResultCode expectedResultCode =
                                        CreateUpdateKYCRequestResultCode::SUCCESS);

        protected:
            CreateUpdateKYCRequestResult
            checkApprovedCreation(CreateUpdateKYCRequestResult opResult, AccountID accountToUpdateKYC,
                                  LedgerDelta::KeyEntryMap stateBeforeOp);

            ReviewableRequestFrame::pointer
            createReviewableChangeKYCRequest(UpdateKYCRequest request, uint64 requestID);

            xdr::pointer<string64> getReference();
        };
    }
}