#pragma once

#include "overlay/StellarXDR.h"
#include "TxHelper.h"

namespace stellar
{

namespace txtest
{
    class ManageExternalSystemIDProviderTestHelper: TxHelper
    {
    public:
        explicit ManageExternalSystemIDProviderTestHelper(TestManager::pointer testManager);

        TransactionFramePtr
        createManageExternalSystemIDProviderTx(Account& source,
                                               ExternalSystemType type,
                                               std::string data,
                                               ManageExternalSystemIdProviderAction action =
                                               ManageExternalSystemIdProviderAction::CREATE,
                                               Account* signer = nullptr);

        ManageExternalSystemIdProviderResult
        applyManageExternalSystemIDProviderTx(Account& source,
                                              ExternalSystemType type,
                                              std::string data,
                                              ManageExternalSystemIdProviderAction action =
                                              ManageExternalSystemIdProviderAction::CREATE,
                                              ManageExternalSystemIdProviderResultCode expectedResultCode =
                                              ManageExternalSystemIdProviderResultCode::SUCCESS,
                                              Account* signer = nullptr);
    };
}
}
