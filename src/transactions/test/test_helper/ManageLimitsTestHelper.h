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
    class ManageLimitsTestHelper : TxHelper
    {
        public:
            explicit ManageLimitsTestHelper(TestManager::pointer testManager);

            TransactionFramePtr createManageLimitsTx(Account& source, ManageLimitsOp& manageLimitsOp);

            ManageLimitsOp
            createManageLimitsOp(AssetCode asset, StatsOpType type,
                                 bool isConvertNeeded, uint64_t daily,
                                 uint64_t weekly, uint64_t monthly, uint64_t annual,
                                 xdr::pointer<AccountID> accountID = nullptr,
                                 xdr::pointer<AccountType> accountType = nullptr);

            void applyManageLimitsTx(Account &source, ManageLimitsOp& manageLimitsOp,
                                     ManageLimitsResultCode expectedResult = ManageLimitsResultCode::SUCCESS);
    };
}
}