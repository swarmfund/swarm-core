#pragma once

#include "TxHelper.h"

namespace stellar
{
namespace txtest
{
class ManageContractRequestTestHelper : TxHelper
{
public:
    explicit ManageContractRequestTestHelper(TestManager::pointer testManager);


    TransactionFramePtr createManageContractRequest(Account& source, ManageContractRequestOp& manageContractRequestOp);

    ManageContractRequestResult applyManageContractRequest(Account& source,
                                                         ManageContractRequestOp& manageContractRequestOp,
                                                         ManageContractRequestResultCode expectedResult = ManageContractRequestResultCode::SUCCESS);


    ManageContractRequestOp createContractRequest(AccountID customer, AccountID escrow,
                                                  uint64_t startTime,
                                                  uint64_t endTime, longstring details);

    ManageContractRequestOp createRemoveContractRequest(uint64_t& requestID);

};

}
}
