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

class OfferManager
{
public:
    // delets offer and unlock locked assets by that offer
    static void deleteOffer(OfferFrame::pointer offerFrame, Database& db, LedgerDelta& delta);
    // delets all offers and unlocks locked assets by that offers   
    static void deleteOffers(std::vector<OfferFrame::pointer> offers, Database& db, LedgerDelta& delta);
    // Builds offer frame base on ManageOfferOp
    static OfferFrame::pointer buildOffer(AccountID const& sourceID, ManageOfferOp const& op, AssetCode const& base, AssetCode const& quote);
    // Builds ManageOfferOp base on based params
    static ManageOfferOp buildManageOfferOp(BalanceID const& baseBalance, BalanceID const& quoteBalance, bool const isBuy, int64_t const amount,
        int64_t const price, int64_t const fee, uint64_t const offerID, uint64_t const orderBookID);
    // Calculates quote amount. Returns 0 if fails due to overflow
    static int64_t calculateQuoteAmount(int64_t const baseAmount, int64_t const price);

    };
}
