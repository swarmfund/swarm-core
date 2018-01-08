#pragma once

// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "overlay/StellarXDR.h"
#include "TxHelper.h"

namespace stellar
{
namespace txtest
{
    class SetLimitsTestHelper : TxHelper
    {
    public:
        explicit SetLimitsTestHelper(TestManager::pointer testManager);

        TransactionFramePtr createSetLimitsTx(Account& source, AccountID* account,
        AccountType* accountType, Limits limits);

        void applySetLimitsTx(Account& source, AccountID* account, AccountType* accountType,
        Limits limits, SetLimitsResultCode expectedResult = SetLimitsResultCode::SUCCESS);
    };
}
}