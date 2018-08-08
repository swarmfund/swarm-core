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
    
    ManageContractOp createAddDetailsOp(uint64_t& contractID, longstring& details);
    ManageContractOp createConfirmOp(uint64_t& contractID);
    ManageContractOp createStartDisputeOp(uint64_t& contractID, longstring disputeReason);
    ManageContractOp createResolveDisputeOp(uint64_t &contractID, bool isRevert);

    ManageContractResult applyManageContractTx(Account &source, ManageContractOp manageContractOp,
                               ManageContractResultCode expectedResult = ManageContractResultCode::SUCCESS);
};
}
}