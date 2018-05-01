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
        applyCreateExternalSystemAccountIDPoolEntryTx(Account &source,
                                                      ManageExternalSystemAccountIdPoolEntryOp::_actionInput_t
                                                      actionInput,
                                                      ManageExternalSystemAccountIdPoolEntryAction action =
                                                      ManageExternalSystemAccountIdPoolEntryAction::CREATE,
                                                      ManageExternalSystemAccountIdPoolEntryResultCode expectedResultCode =
                                                      ManageExternalSystemAccountIdPoolEntryResultCode::SUCCESS,
                                                      Account *signer = nullptr);

        ManageExternalSystemAccountIdPoolEntryResult
        applyDeleteExternalSystemAccountIDPoolEntryTx(Account &source,
                                                      ManageExternalSystemAccountIdPoolEntryOp::_actionInput_t
                                                      actionInput,
                                                      ManageExternalSystemAccountIdPoolEntryAction action =
                                                      ManageExternalSystemAccountIdPoolEntryAction::DELETE,
                                                      ManageExternalSystemAccountIdPoolEntryResultCode expectedResultCode =
                                                      ManageExternalSystemAccountIdPoolEntryResultCode::SUCCESS,
                                                      Account *signer = nullptr);
    public:
        explicit ManageExternalSystemAccountIDPoolEntryTestHelper(TestManager::pointer testManager);


        ManageExternalSystemAccountIdPoolEntryResult
        createExternalSystemAccountIdPoolEntry(Account &source, int32 type, std::string data,
                                               ManageExternalSystemAccountIdPoolEntryResultCode expectedResultCode =
                                               ManageExternalSystemAccountIdPoolEntryResultCode::SUCCESS,
                                               Account *signer = nullptr);
        ManageExternalSystemAccountIdPoolEntryResult
        deleteExternalSystemAccountIdPoolEntry(Account &source,
                                               ManageExternalSystemAccountIdPoolEntryResultCode expectedResultCode =
                                               ManageExternalSystemAccountIdPoolEntryResultCode::SUCCESS,
                                               uint64 poolEntryID = 0, Account *signer = nullptr);
    };
}
}
