#pragma once

// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include <ledger/ReviewableRequestFrame.h>
#include "overlay/StellarXDR.h"
#include "TxHelper.h"

namespace stellar
{
namespace txtest
{
class WithdrawRequestHelper : TxHelper
{
private:
    // returns true if there are stats asset and corresponding asset pair
    bool canCalculateStats(AssetCode baseAsset);

    void validateStatsChange(StatisticsV2Frame::pointer statsAfter, ReviewableRequestFrame::pointer withdrawRequest);
public:
    WithdrawRequestHelper(TestManager::pointer testManager);

    CreateWithdrawalRequestResult applyCreateWithdrawRequest(
        Account& source, WithdrawalRequest request,
        CreateWithdrawalRequestResultCode expectedResult =
            CreateWithdrawalRequestResultCode::SUCCESS);

    static WithdrawalRequest createWithdrawRequest(BalanceID balance, uint64_t amount,
                                            Fee fee,
                                            std::string externalDetails,
                                            AssetCode autoConversionDestAsset,
                                            uint64_t expectedAutoConversion);

    TransactionFramePtr createWithdrawalRequestTx(
        Account& source, WithdrawalRequest request);
};
}
}
