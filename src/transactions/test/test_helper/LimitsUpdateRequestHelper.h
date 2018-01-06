#pragma once

// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ledger/AccountLimitsFrame.h"
#include <ledger/ReviewableRequestFrame.h>
#include "overlay/StellarXDR.h"
#include "TxHelper.h"

namespace stellar
{
namespace txtest
{
class LimitsUpdateRequestHelper : TxHelper
{
public:
    explicit LimitsUpdateRequestHelper(TestManager::pointer testManager);

    SetOptionsResult applyCreateLimitsUpdateRequest(
            Account& source, LimitsUpdateRequest request,
            SetOptionsResultCode expectedResult =
                SetOptionsResultCode::SUCCESS);

    static LimitsUpdateRequest createLimitsUpdateRequest(Hash documentHash,
                                                         std::string externalDetails);

    TransactionFramePtr createLimitsUpdateRequestTx(Account& source, LimitsUpdateRequest request);
};
}
}