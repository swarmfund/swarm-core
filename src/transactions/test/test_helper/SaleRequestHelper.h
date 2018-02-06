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

    ReviewRequestResult createApprovedSale(Account& root, Account & source, SaleCreationRequest request);

    SaleCreationRequestQuoteAsset createSaleQuoteAsset(AssetCode asset, uint64_t price);

    CreateSaleCreationRequestResult applyCreateSaleRequest(
        Account& source, uint64_t requestID, SaleCreationRequest request,
        CreateSaleCreationRequestResultCode expectedResult =
        CreateSaleCreationRequestResultCode::SUCCESS);
    static SaleCreationRequest createSaleRequest(AssetCode base,
        AssetCode defaultQuoteAsset, const uint64_t startTime, const uint64_t endTime,
        const uint64_t softCap, const uint64_t hardCap, std::string details, std::vector<SaleCreationRequestQuoteAsset> quoteAssets);

    TransactionFramePtr createSaleRequestTx(
        Account& source, uint64_t requestID, SaleCreationRequest request);

    ManageSaleResult applyManageSale(Account &source, uint64_t saleID, ManageSaleAction action,
                                     ManageSaleResultCode expectedResult = ManageSaleResultCode::SUCCESS);

    TransactionFramePtr createManageSaleTx(Account &source, uint64_t saleID, ManageSaleAction action);

};
}
}
