#pragma once

#include "overlay/StellarXDR.h"
#include "TxHelper.h"

namespace stellar
{

namespace txtest
{
    class ManageExternalSystemAccountIDPoolEntryTestHelper: TxHelper
    {
        TransactionFramePtr
        createManageExternalSystemAccountIDPoolEntryTx(Account &source,
                                                       ManageExternalSystemAccountIdPoolEntryOp::_actionInput_t
                                                       actionInput,
                                                       ManageExternalSystemAccountIdPoolEntryAction action =
                                                       ManageExternalSystemAccountIdPoolEntryAction::CREATE,
                                                       Account *signer = nullptr);

        ManageExternalSystemAccountIdPoolEntryResult
        applyManageExternalSystemAccountIDPoolEntryTx(Account &source,
                                                      ManageExternalSystemAccountIdPoolEntryOp::_actionInput_t
                                                      actionInput,
                                                      ManageExternalSystemAccountIdPoolEntryAction action =
                                                      ManageExternalSystemAccountIdPoolEntryAction::CREATE,
                                                      ManageExternalSystemAccountIdPoolEntryResultCode expectedResultCode =
                                                      ManageExternalSystemAccountIdPoolEntryResultCode::SUCCESS,
                                                      Account *signer = nullptr);
    public:
        explicit ManageExternalSystemAccountIDPoolEntryTestHelper(TestManager::pointer testManager);


        ManageExternalSystemAccountIdPoolEntryResult
        createExternalSystemAccountIdPoolEntry(Account &source, ExternalSystemType type, std::string data,
                                               ManageExternalSystemAccountIdPoolEntryResultCode expectedResultCode =
                                               ManageExternalSystemAccountIdPoolEntryResultCode::SUCCESS,
                                               Account *signer = nullptr);
    };
}
}
