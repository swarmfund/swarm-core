// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "FeesManager.h"
#include "ledger/FeeHelper.h"

namespace stellar
{
FeeManager::FeeResult
FeeManager::calculateFeeForAccount(const AccountFrame::pointer account,
                                   FeeType const feeType, AssetCode const &asset,
                                   int64_t const subtype, uint64_t const amount, Database &db)
{
    auto result = FeeResult{ 0, 0, 0, false };
    auto feeFrame = FeeHelper::Instance()->loadForAccount(feeType, asset, subtype, account, amount, db);
    if (!feeFrame)
    {
        return result;
    }

    result.fixedFee = feeFrame->getFixedFee();
    result.percentFee = feeFrame->getPercentFee();
    result.isOverflow = !feeFrame->calculatePercentFee(amount, result.calculatedPercentFee, ROUND_UP);
    return result;
}

FeeManager::FeeResult
FeeManager::calculateOfferFeeForAccount(const AccountFrame::pointer account,
                                        AssetCode const& quoteAsset,
                                        uint64_t const quoteAmount, Database& db)
{
    return calculateFeeForAccount(account, FeeType::OFFER_FEE, quoteAsset, FeeFrame::SUBTYPE_ANY, quoteAmount, db);
}

FeeManager::FeeResult
FeeManager::calculateCapitalDeploymentFeeForAccount(const AccountFrame::pointer account,
                                                    AssetCode const& quoteAsset,
                                                    uint64_t const quoteAmount, Database& db)
{
    return calculateFeeForAccount(account, FeeType::CAPITAL_DEPLOYMENT_FEE, quoteAsset,
                                  FeeFrame::SUBTYPE_ANY, quoteAmount, db);
}
}
