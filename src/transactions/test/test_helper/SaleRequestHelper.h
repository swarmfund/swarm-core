#pragma once

// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "overlay/StellarXDR.h"
#include "TxHelper.h"
#include "transactions/CreateSaleCreationRequestOpFrame.h"

namespace stellar
{
namespace txtest
{
class SaleRequestHelper : TxHelper
{
public:
    SaleRequestHelper(TestManager::pointer testManager);

    uint64_t createApproveSale(Account& root, Account & source, SaleCreationRequest request);

    CreateSaleCreationRequestResult applyCreateSaleRequest(
        Account& source, uint64_t requestID, SaleCreationRequest request,
        CreateSaleCreationRequestResultCode expectedResult =
        CreateSaleCreationRequestResultCode::SUCCESS);
    static SaleCreationRequest createSaleRequest(AssetCode base, AssetCode quote, std::string name, uint64_t startTime, uint64_t endTime,
        uint64_t price, uint64_t softCap, uint64_t hardCap, std::string details);

    TransactionFramePtr createSaleRequestTx(
        Account& source, uint64_t requestID, SaleCreationRequest request);
};
}
}
