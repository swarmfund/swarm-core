#pragma once

// Copyright 2015 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ledger/OfferFrame.h"
#include "ledger/AssetPairFrame.h"
#include <functional>
#include <vector>
#include "transactions/AccountManager.h"

namespace stellar
{

class FeeManager
{
public:
    struct FeeResult {
        uint64_t fixedFee;
        uint64_t percentFee;
        uint64_t calculatedPercentFee;
        bool isOverflow;
    };

    static FeeResult calculateFeeForAccount(const AccountFrame::pointer account, FeeType const feeType,
                                            AssetCode const &asset,
                                            int64_t const subtype, uint64_t const amount, Database &db);

    static FeeResult calculateOfferFeeForAccount(AccountFrame::pointer account, AssetCode const& quoteAsset,
                                                 uint64_t const quoteAmount, Database& db);

    static FeeResult calculateCapitalDeploymentFeeForAccount(const AccountFrame::pointer account,
                                                      AssetCode const& quoteAsset,
                                                      uint64_t const quoteAmount, Database& db);

};
}
