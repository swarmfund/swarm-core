#pragma once

#include "overlay/StellarXDR.h"
#include "TxHelper.h"
#include "transactions/BindExternalSystemAccountIdOpFrame.h"

namespace stellar
{

namespace txtest
{
    class BindExternalSystemAccountIdTestHelper : TxHelper
    {
    public:
        explicit BindExternalSystemAccountIdTestHelper(TestManager::pointer testManager);

        TransactionFramePtr createBindExternalSystemAccountIdTx(Account &source, int32 externalSystemType,
                                                                Account *signer = nullptr);

        BindExternalSystemAccountIdResult applyBindExternalSystemAccountIdTx(Account& source,
                                                               int32 externalSystemType,
                                                               BindExternalSystemAccountIdResultCode expectedResultCode =
                                                               BindExternalSystemAccountIdResultCode::SUCCESS,
                                                               Account* signer = nullptr);
    };
}

}