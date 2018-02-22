#pragma once

#include "overlay/StellarXDR.h"
#include "TxHelper.h"

namespace stellar
{

namespace txtest
{
    class BindExternalSystemAccountIdTestHelper : TxHelper
    {
    public:
        explicit BindExternalSystemAccountIdTestHelper(TestManager::pointer testManager);

        TransactionFramePtr createBindExternalSystemAccountIdTx(Account& source, ExternalSystemType externalSystemType,
                                                                Account* signer = nullptr);

        BindExternalSystemAccountIdResult applyBindExternalSystemAccountIdTx(Account& source,
                                                               ExternalSystemType externalSystemType,
                                                               BindExternalSystemAccountIdResultCode expectedResultCode =
                                                               BindExternalSystemAccountIdResultCode::SUCCESS,
                                                               Account* signer = nullptr);
    };
}

}