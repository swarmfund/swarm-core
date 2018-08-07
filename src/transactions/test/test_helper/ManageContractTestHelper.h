#pragma once

// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "overlay/StellarXDR.h"
#include "util/optional.h"
#include "TxHelper.h"

namespace stellar
{
namespace txtest
{

class ManageContractTestHelper : TxHelper
{
public:
    explicit ManageContractTestHelper(TestManager::pointer testManager);

    TransactionFramePtr createManageContractTx(Account &source, ManageContractOp manageContractOp);
    
    ManageContractOp createAddDetailsOp(Account &source, uint64_t& contractID, longstring& details);
    ManageContractOp createConfirmOp(Account &source, uint64_t& contractID);
    ManageContractOp createStartDisputeOp(Account &source, uint64_t& contractID, longstring& disputeReason);

    ManageContractResult applyManageContractTx(Account &source, ManageContractOp manageContractOp,
                               ManageContractResultCode expectedResult = ManageContractResultCode::SUCCESS);
};
}
}