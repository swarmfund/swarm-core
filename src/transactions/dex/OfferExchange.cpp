// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "OfferExchange.h"
#include "database/Database.h"
#include "ledger/LedgerDelta.h"
#include "ledger/LedgerManager.h"
#include "ledger/BalanceHelperLegacy.h"
#include "ledger/OfferHelper.h"
#include "util/Logging.h"
#include "xdrpp/printer.h"

namespace stellar
{
OfferExchange::OfferExchange(AccountManager& accountManager, LedgerDelta& delta,
                             LedgerManager& ledgerManager,
                             AssetPairFrame::pointer assetPair,
                             BalanceFrame::pointer commissionBalance, uint64_t orderBookID)
    : mDelta(delta)
    , mLedgerManager(ledgerManager)
    , mAccountManager(accountManager)
    , mAssetPair(assetPair)
    , mCommissionBalance(commissionBalance)
{
    mNow = mLedgerManager.getCloseTime();
    mFeePaidByA = 0;
    mOrderBookID = orderBookID;
}


bool OfferExchange::getQuoteAmountBasedOnFee(OfferEntry const& offer,
                                             int64_t& quoteAmountBasedOnFee)
{
    int64_t percentFee = offer.percentFee;
    if (percentFee == 0)
    {
        quoteAmountBasedOnFee = INT64_MAX;
        return true;
    }

    return bigDivide(quoteAmountBasedOnFee, offer.fee, 100 * ONE, percentFee,
                     ROUND_DOWN);
}

int64 OfferExchange::getMatchPrice(OfferEntry const& buy,
                                   OfferEntry const& sell)
{
    // If volume is the same between best sell order and Best Buy order, then you execute order matching on buy price
    if (buy.baseAmount == sell.baseAmount)
        return buy.price;

    // If Best Buy order of bigger volume than best sell order, then you execute order matching on buy price
    if (buy.baseAmount > sell.baseAmount)
        return buy.price;

    // If best sell order of bigger volume than Best Buy order, then you execute order matching on sell price
    return sell.price;
}

ExchangeResult OfferExchange::exchange(OfferEntry& offerA, OfferEntry& offerB)
{
    OfferEntry& buyingOffer = offerA.isBuy ? offerA : offerB;
    OfferEntry& sellingOffer = offerA.isBuy ? offerB : offerA;
    int64_t matchPrice = getMatchPrice(buyingOffer, sellingOffer);

    int64_t quoteAmountBasedOnLockedFee;
    if (!getQuoteAmountBasedOnFee(buyingOffer, quoteAmountBasedOnLockedFee))
    {
        ExchangeResult result{};
        result.type = ExchangeResultType::RESULT_OVERFLOW;
        return result;
    }

    int64_t buyingOfferQuoteAmount = std::min(quoteAmountBasedOnLockedFee,
                                              buyingOffer.quoteAmount);

    auto match = exchange(buyingOffer.baseAmount, buyingOfferQuoteAmount,
                          sellingOffer.baseAmount, sellingOffer.quoteAmount,
                          matchPrice);
    if (!setFeeToPay(match.buyerFee, match.quoteDelta, buyingOffer.percentFee)
        || !setFeeToPay(match.sellerFee, match.quoteDelta,
                        sellingOffer.percentFee))
    {
        ExchangeResult result{};
        result.type = ExchangeResultType::RESULT_OVERFLOW;
        return result;
    }

    if (match.sellerFee >= match.quoteDelta)
    {
        LOG(ERROR) << "offerA: " << xdr::xdr_to_string(offerA) << std::endl << "offerB: " << xdr::xdr_to_string(offerB);
        throw std::runtime_error("Fee to be charged can't be >= quote delta");
    }

    return match;
}

bool OfferExchange::setFeeToPay(int64_t& feeToPay, int64_t quoteAmount,
                                int64_t percentFee)
{
    return bigDivide(feeToPay, quoteAmount, percentFee, 100 * ONE, ROUND_UP);
}

// TODO handle overflows
ExchangeResult OfferExchange::exchange(int64_t buyerBase, int64_t buyerQuote,
                                       int64_t sellerBase, int64_t sellerQuote,
                                       int64_t matchPrice)
{
    ExchangeResult result{};
    result.matchPrice = matchPrice;
    // find smallest base and calculate quote based on it
    result.baseDelta = std::min(buyerBase, sellerBase);
    if (!bigDivide(result.quoteDelta, result.baseDelta, matchPrice, ONE,
                   ROUND_UP))
    {
        result.type = ExchangeResultType::RESULT_OVERFLOW;
        return result;
    }

    // ok to return now, as we do not exceed any limits
    if (result.quoteDelta <= buyerQuote)
    {
        result.type = ExchangeResultType::NORMAL;
        return result;
    }

    result.quoteDelta = buyerQuote;

    // calculate baseDelta based on quoteDelta
    if (!bigDivide(result.baseDelta, result.quoteDelta, ONE, matchPrice,
                   ROUND_DOWN))
    {
        result.type = ExchangeResultType::RESULT_OVERFLOW;
        return result;
    }

    // now we can try to make quote delta more accurate based on new base delta
    int64_t quoteDeltaCandiate = 0;
    if (!bigDivide(quoteDeltaCandiate, result.baseDelta, matchPrice, ONE,
                   ROUND_UP))
    {
        result.type = ExchangeResultType::RESULT_OVERFLOW;
        return result;
    }

    if (quoteDeltaCandiate < result.quoteDelta)
        result.quoteDelta = quoteDeltaCandiate;

    result.type = ExchangeResultType::NORMAL;
    return result;
}

void OfferExchange::markOfferAsTaken(OfferFrame& offer,
                                     BalanceFrame::pointer baseBalance,
                                     BalanceFrame::pointer quoteBalance,
                                     Database& db)
{
    EntryHelperProvider::storeDeleteEntry(mDelta, db, offer.getKey());
    unlockBalancesForTakenOffer(offer, baseBalance, quoteBalance);
}

void OfferExchange::unlockBalancesForTakenOffer(OfferFrame& offer,
                                                BalanceFrame::pointer
                                                baseBalance,
                                                BalanceFrame::pointer
                                                quoteBalance)
{
    BalanceFrame::Result result;
    if (offer.getOffer().isBuy)
    {
        int64_t lockedAmount = offer.getOffer().quoteAmount + offer.getOffer().
                                                                    fee;
        result = quoteBalance->lockBalance(-lockedAmount);
    }
    else
    {
        result = baseBalance->lockBalance(-offer.getOffer().baseAmount);
    }

    if (result != BalanceFrame::Result::SUCCESS)
        throw std::runtime_error("Failed to mark offer as taken");
}

BalanceFrame::pointer OfferExchange::loadBalance(
    BalanceID& balanceID, Database& db)
{
    auto balanceHelper = BalanceHelperLegacy::Instance();
    BalanceFrame::pointer balance = balanceHelper->loadBalance(balanceID, db,
                                                               &mDelta);
    if (!balance)
    {
        throw std::runtime_error(
                                 "invalid database state: offer must have matching balance");
    }

    return balance;
}

bool OfferExchange::isOfferPriceMeetAssetPairRestrictions(
    AssetPairFrame::pointer assetPair, int64_t offerPrice)
{
    int64_t minAllowedPrice = assetPair->getMinAllowedPrice();
    return offerPrice >= minAllowedPrice;
}

void OfferExchange::offerMatched(OfferEntry& offer,
                                 BalanceFrame::pointer baseBalance,
                                 BalanceFrame::pointer quoteBalance,
                                 ExchangeResult match)
{
    offer.baseAmount -= match.baseDelta;
    offer.quoteAmount -= match.quoteDelta;

    bool isBalanceValid = false;
    if (offer.isBuy)
    {
        assert(match.buyerFee >= 0);
        int64_t amountToCharge = match.quoteDelta + match.buyerFee;
        isBalanceValid = baseBalance->addBalance(match.baseDelta) &&
                         quoteBalance->addLocked(-amountToCharge);
        offer.fee -= match.buyerFee;
    }
    else
    {
        // if we are selling we can receive more quote then we've expected
        offer.quoteAmount = std::max(offer.quoteAmount, int64_t(0));

        assert(match.sellerFee >= 0);
        int64_t amountToAdd = match.quoteDelta - match.sellerFee;
        assert(amountToAdd > 0);
        isBalanceValid = baseBalance->addLocked(-match.baseDelta) &&
                         quoteBalance->addBalance(amountToAdd);
    }

    if (!isBalanceValid)
        throw std::
            runtime_error("Failed to handel offer match - unable to unlock balace");

    if (offer.baseAmount < 0 || offer.quoteAmount < 0 || offer.fee < 0)
        throw std::
            runtime_error("Failed to handel offer match - unable to update base or quote amount for offer");
}

ClaimOfferAtom OfferExchange::createOfferClaim(OfferEntry const& offerB,
                                               ExchangeResult const& match)
{
    ClaimOfferAtom result;
    result.bAccountID = offerB.ownerID;
    result.baseBalance = offerB.baseBalance;
    result.quoteBalance = offerB.quoteBalance;
    result.bFeePaid = offerB.isBuy ? match.buyerFee : match.sellerFee;
    result.aFeePaid = !offerB.isBuy ? match.buyerFee : match.sellerFee;
    result.baseAmount = match.baseDelta;
    result.quoteAmount = match.quoteDelta;
    result.currentPrice = match.matchPrice;
    result.offerID = offerB.offerID;
    return result;
}

OfferExchange::CrossOfferResult OfferExchange::crossOffer(
    OfferEntry& offerA, BalanceFrame::pointer baseBalanceA,
    BalanceFrame::pointer quoteBalanceA, OfferFrame& offerFrameB)
{
    // we're about to make changes to the offer
    mDelta.recordEntry(offerFrameB);

    Database& db = mLedgerManager.getDatabase();

    OfferEntry& offerB = offerFrameB.getOffer();
    BalanceFrame::pointer baseBalanceB = loadBalance(offerB.baseBalance, db);
    bool isOfferValid = true;

    // if first balance is not valid - no need to load second
    BalanceFrame::pointer quoteBalanceB = loadBalance(offerB.quoteBalance, db);

    isOfferValid = isOfferValid &&
                   isOfferPriceMeetAssetPairRestrictions(mAssetPair,
                                                         offerFrameB.
                                                         getPrice());
    // one of the balances is not valid for trading or offer does not meet asset pair restrictions, so canceling offer
    if (!isOfferValid)
    {
        markOfferAsTaken(offerFrameB, baseBalanceB, quoteBalanceB, db);
        EntryHelperProvider::storeChangeEntry(mDelta, db, baseBalanceB->mEntry);
        EntryHelperProvider::storeChangeEntry(mDelta, db, baseBalanceB->mEntry);
        return eOfferTaken;
    }

    auto exchangeResult = exchange(offerA, offerB);
    if (exchangeResult.type == ExchangeResultType::RESULT_OVERFLOW)
        return eOfferCantConvert;

    if (exchangeResult.baseDelta == 0 || exchangeResult.quoteDelta == 0)
    {
        LOG(ERROR) << "baseAmountA: " << offerA.baseAmount << " quoteAmountA: "
            << offerA.quoteAmount << " baseAmountB: " << offerB.baseAmount <<
            " quoteAmountB:" << offerB.quoteAmount << " matchingPrice: " <<
            offerFrameB.getPrice() << " offerB is buy:" << offerB.isBuy;
        throw std::runtime_error("After match baseDelta or quoteDelta is zero");
    }

    offerMatched(offerA, baseBalanceA, quoteBalanceA, exchangeResult);
    offerMatched(offerB, baseBalanceB, quoteBalanceB, exchangeResult);

    mOfferTrail.push_back(createOfferClaim(offerB, exchangeResult));
    mFeePaidByA += (offerA.isBuy
                        ? exchangeResult.buyerFee
                        : exchangeResult.sellerFee);

    assert(mCommissionBalance->addBalance(exchangeResult.buyerFee));
    assert(mCommissionBalance->addBalance(exchangeResult.sellerFee));

    if (!offerNeedsMore(offerB))
    {
        // entire offer is taken
        markOfferAsTaken(offerFrameB, baseBalanceB, quoteBalanceB, db);
        EntryHelperProvider::storeChangeEntry(mDelta, db, baseBalanceB->mEntry);
        EntryHelperProvider::
            storeChangeEntry(mDelta, db, quoteBalanceB->mEntry);
        return eOfferTaken;
    }

    EntryHelperProvider::storeChangeEntry(mDelta, db, offerFrameB.mEntry);
    EntryHelperProvider::storeChangeEntry(mDelta, db, baseBalanceB->mEntry);
    EntryHelperProvider::storeChangeEntry(mDelta, db, quoteBalanceB->mEntry);
    return eOfferPartial;
}

bool OfferExchange::offerNeedsMore(OfferEntry& offer)
{
    // check how much can we buy
    if (offer.isBuy)
    {
        int64_t quoteAmountBasedOnFee;
        if (!getQuoteAmountBasedOnFee(offer, quoteAmountBasedOnFee))
            return false;

        int64_t availableQuoteAmount = std::min(quoteAmountBasedOnFee,
                                                offer.quoteAmount);
        int64_t canBuy = 0;
        if (!bigDivide(canBuy, availableQuoteAmount, ONE, offer.price,
                       ROUND_DOWN) || canBuy == 0)
            return false;
    }
    return offer.baseAmount > 0 && offer.quoteAmount > 0;
}

OfferExchange::ConvertResult
OfferExchange::convertWithOffers(
    OfferEntry& offerA, BalanceFrame::pointer baseBalanceA,
    BalanceFrame::pointer quoteBalanceA,
    std::function<OfferFilterResult(OfferFrame const&)> filter)
{
    const size_t OFFERS_TO_TAKE = 5;
    Database& db = mLedgerManager.getDatabase();

    size_t offerOffset = 0;

    while (offerNeedsMore(offerA))
    {
        std::vector<OfferFrame::pointer> retList;
        auto offerHelper = OfferHelper::Instance();
        offerHelper->loadBestOffers(OFFERS_TO_TAKE, offerOffset,
                                    mAssetPair->getBaseAsset(),
                                    mAssetPair->getQuoteAsset(), mOrderBookID, !offerA.isBuy,
                                    retList, db);

        offerOffset += retList.size();

        for (auto& offerB : retList)
        {
            if (filter)
            {
                OfferFilterResult r = filter(*offerB);
                switch (r)
                {
                case eKeep:
                    break;
                case eStop:
                    return eFilterStop;
                case eSkip:
                    continue;
                }
            }

            CrossOfferResult cor = crossOffer(offerA, baseBalanceA,
                                              quoteBalanceA, *offerB);

            switch (cor)
            {
            case eOfferTaken:
                assert(offerOffset > 0);
                offerOffset--; // adjust offset as an offer was deleted
                break;
            case eOfferPartial:
                break;
            case eOfferCantConvert:
                return ePartial;
            }

            if (!offerNeedsMore(offerA))
            {
                return eOK;
            }

            if (cor == eOfferPartial)
            {
                return ePartial;
            }
        }

        // still stuff to fill but no more offers
        if (offerNeedsMore(offerA) && retList.size() < OFFERS_TO_TAKE)
        {
            return eOK;
        }
    }
    return eOK;
}
}
