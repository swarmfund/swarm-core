// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "OfferManager.h"
#include "ledger/BalanceHelper.h"
#include "ledger/LedgerDelta.h"
#include "xdrpp/printer.h"
#include "ledger/FeeHelper.h"

namespace stellar
{
void OfferManager::deleteOffer(OfferFrame::pointer offerFrame, Database& db,
    LedgerDelta& delta)
{
    const auto balanceID = offerFrame->getLockedBalance();
    auto balanceFrame = BalanceHelper::Instance()->loadBalance(balanceID, db, &delta);
    if (!balanceFrame)
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Invalid database state: failed to load balance to cancel order: " << xdr::xdr_to_string(offerFrame->getOffer());
        throw std::runtime_error("Invalid database state: failed to load balance to cancel order");
    }

    const auto amountToUnlock = offerFrame->getLockedAmount();
    if (!balanceFrame->unlock(amountToUnlock))
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Invalid database state: failed to unlocked locked amount for offer: " << xdr::xdr_to_string(offerFrame->getOffer());
        throw std::runtime_error("Invalid database state: failed to unlocked locked amount for offer");
    }

    EntryHelperProvider::storeDeleteEntry(delta, db, offerFrame->getKey());
    EntryHelperProvider::storeChangeEntry(delta, db, balanceFrame->mEntry);
}

void OfferManager::deleteOffers(std::vector<OfferFrame::pointer> offers,
    Database& db, LedgerDelta& delta)
{
    for (auto& offer : offers)
    {
        delta.recordEntry(*offer);
        deleteOffer(offer, db, delta);
    }
}

OfferFrame::pointer OfferManager::buildOffer(AccountID const& sourceID, ManageOfferOp const& op,
    AssetCode const& base, AssetCode const& quote)
{
    OfferEntry o;
    o.orderBookID = op.orderBookID;
    o.base = base;
    o.baseAmount = op.amount;
    o.baseBalance = op.baseBalance;
    o.quoteBalance = op.quoteBalance;
    o.isBuy = op.isBuy;
    o.offerID = op.offerID;
    o.ownerID = sourceID;
    o.price = op.price;
    o.quote = quote;
    o.quoteAmount = calcualteQuoteAmount(op.amount, op.price);

    LedgerEntry le;
    le.data.type(LedgerEntryType::OFFER_ENTRY);
    le.data.offer() = o;
    return std::make_shared<OfferFrame>(le);
}

ManageOfferOp OfferManager::buildManageOfferOp(BalanceID const& baseBalance,
    BalanceID const& quoteBalance, bool const isBuy, int64_t const amount,
    int64_t const price, int64_t const fee, uint64_t const offerID,
    uint64_t const orderBookID)
{
    ManageOfferOp op;
    op.baseBalance = baseBalance;
    op.quoteBalance = quoteBalance;
    op.isBuy = isBuy;
    op.amount = amount;
    op.price = price;
    op.fee = fee;
    op.offerID = offerID;
    op.orderBookID = orderBookID;
    return op;
}

int64_t OfferManager::calcualteQuoteAmount(int64_t const baseAmount,
    int64_t const price)
{
    // 1. Check quote amount fits minimal presidion 
    int64_t result;
    if (!bigDivide(result, baseAmount, price, ONE, ROUND_DOWN))
        return 0;

    if (result == 0)
        return 0;

    // 2. Calculate amount to be spent
    if (!bigDivide(result, baseAmount, price, ONE, ROUND_UP))
        return 0;
    return result;
}
}
