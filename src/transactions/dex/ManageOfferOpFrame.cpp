// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "util/asio.h"
#include "ManageOfferOpFrame.h"
#include "OfferExchange.h"
#include "database/Database.h"
#include "ledger/LedgerDelta.h"
#include "ledger/AccountHelper.h"
#include "ledger/AssetPairHelper.h"
#include "ledger/BalanceHelper.h"
#include "ledger/OfferHelper.h"
#include "ledger/OfferFrame.h"
#include "main/Application.h"
#include "util/Logging.h"
#include "util/types.h"
#include "ManageSaleParticipationOpFrame.h"
#include "OfferManager.h"
#include "transactions/FeesManager.h"

namespace stellar
{
using namespace std;
using xdr::operator==;

// TODO requires refactoring
ManageOfferOpFrame::ManageOfferOpFrame(Operation const& op,
                                       OperationResult& res,
                                       TransactionFrame& parentTx)
    : OperationFrame(op, res, parentTx)
    , mManageOffer(mOperation.body.manageOfferOp())
{
}

BalanceFrame::pointer ManageOfferOpFrame::loadBalanceValidForTrading(
    BalanceID const& balanceID, Database& db,
    LedgerDelta& delta)
{
    auto balanceHelper = BalanceHelper::Instance();
    auto balance = balanceHelper->loadBalance(balanceID, db, &delta);
    if (!balance || !(balance->getAccountID() == getSourceID()))
    {
        innerResult().code(ManageOfferResultCode::BALANCE_NOT_FOUND);
        return nullptr;
    }

    return balance;
}

AssetPairFrame::pointer ManageOfferOpFrame::loadTradableAssetPair(
    Database& db, LedgerDelta& delta)
{
    auto assetPairHelper = AssetPairHelper::Instance();
    AssetPairFrame::pointer assetPair = assetPairHelper->
        loadAssetPair(mBaseBalance->getAsset(), mQuoteBalance->getAsset(), db,
                      &delta);
    if (!assetPair)
        return nullptr;

    if (mManageOffer.orderBookID != SECONDARY_MARKET_ORDER_BOOK_ID)
        return assetPair;

    if (assetPair->checkPolicy(AssetPairPolicy::TRADEABLE_SECONDARY_MARKET))
    {
        return assetPair;
    }

    return nullptr;
}

bool ManageOfferOpFrame::checkOfferValid(Database& db, LedgerDelta& delta)
{
    assert(mManageOffer.amount != 0);

    mBaseBalance =
        loadBalanceValidForTrading(mManageOffer.baseBalance, db, delta);
    if (!mBaseBalance)
        return false;

    mQuoteBalance =
        loadBalanceValidForTrading(mManageOffer.quoteBalance, db, delta);
    if (!mQuoteBalance)
        return false;

    if (mBaseBalance->getAsset() == mQuoteBalance->getAsset())
    {
        innerResult().code(ManageOfferResultCode::ASSET_PAIR_NOT_TRADABLE);
        return false;
    }

    mAssetPair = loadTradableAssetPair(db, delta);
    if (!mAssetPair)
    {
        innerResult().code(ManageOfferResultCode::ASSET_PAIR_NOT_TRADABLE);
        return false;
    }

    return true;
}

OfferExchange::OfferFilterResult ManageOfferOpFrame::filterOffer(const uint64_t price,
                                             OfferFrame const& o)
{
    const auto isPriceBetter = o.getOffer().isBuy
                                   ? o.getPrice() >= price
                                   : o.getPrice() <= price;
    if (!isPriceBetter)
    {
        return OfferExchange::eStop;
    }

    if (o.getOffer().ownerID == getSourceID())
    {
        // we are crossing our own offer
        innerResult().code(ManageOfferResultCode::CROSS_SELF);
        return OfferExchange::eStop;
    }

    return OfferExchange::eKeep;
}

ManageOfferOpFrame* ManageOfferOpFrame::make(Operation const& op,
                                             OperationResult& res,
                                             TransactionFrame& parentTx)
{
    const auto manageOffer = op.body.manageOfferOp();
    if (manageOffer.orderBookID == SECONDARY_MARKET_ORDER_BOOK_ID)
        return new ManageOfferOpFrame(op, res, parentTx);
    return new ManageSaleParticipationOpFrame(op, res, parentTx);
}

std::string ManageOfferOpFrame::getInnerResultCodeAsStr()
{
    const auto result = getResult();
    const auto code = getInnerCode(result);
    return xdr::xdr_traits<ManageOfferResultCode>::enum_name(code);
}

bool ManageOfferOpFrame::deleteOffer(Database& db, LedgerDelta& delta)
{
    auto offer = OfferHelper::Instance()->loadOffer(getSourceID(), mManageOffer.offerID, db,
                                        &delta);
    if (!offer)
    {
        innerResult().code(ManageOfferResultCode::NOT_FOUND);
        return false;
    }

    OfferManager::deleteOffer(offer, db, delta);
    innerResult().code(ManageOfferResultCode::SUCCESS);
    innerResult().success().offer.effect(ManageOfferEffect::DELETED);

    return true;
}

bool ManageOfferOpFrame::createOffer(Application& app, LedgerDelta& delta, LedgerManager& ledgerManager)
{
    Database& db = app.getDatabase();
    if (!checkOfferValid(db, delta))
    {
        return false;
    }

    auto offerFrame = OfferManager::buildOffer(getSourceID(), mManageOffer, mBaseBalance->getAsset(),
        mQuoteBalance->getAsset());
    if (!offerFrame)
    {
        innerResult().code(ManageOfferResultCode::OFFER_OVERFLOW);
        return false;
    }

    auto& offer = offerFrame->getOffer();
    offer.createdAt = ledgerManager.getCloseTime();
    const auto feeResult = FeeManager::calculateOfferFeeForAccount(mSourceAccount, mQuoteBalance->getAsset(), offer.quoteAmount, db);
    if (feeResult.isOverflow)
    {
        innerResult().code(ManageOfferResultCode::OFFER_OVERFLOW);
        return false;
    }

    offer.percentFee = feeResult.percentFee;
    offer.fee = feeResult.calculatedPercentFee;

    const bool isFeeCorrect = offer.fee <= mManageOffer.fee;
    if (!isFeeCorrect)
    {
        innerResult().code(ManageOfferResultCode::MALFORMED);
        return false;
    }

    offer.fee = mManageOffer.fee;

    if (offer.quoteAmount <= offer.fee)
    {
        innerResult().code(ManageOfferResultCode::MALFORMED);
        return false;
    }

    if (!lockSellingAmount(offer))
    {
        innerResult().code(ManageOfferResultCode::UNDERFUNDED);
        return false;
    }

    innerResult().code(ManageOfferResultCode::SUCCESS);

    BalanceFrame::pointer commissionBalance = AccountManager::loadOrCreateBalanceFrameForAsset(app.getCommissionID(), mAssetPair->getQuoteAsset(), db, delta);

    AccountManager accountManager(app, db, delta, ledgerManager);

    OfferExchange oe(accountManager, delta, ledgerManager, mAssetPair,
        commissionBalance, mManageOffer.orderBookID);

    int64_t price = offer.price;
    const OfferExchange::ConvertResult r = oe.convertWithOffers(offer,
        mBaseBalance,
        mQuoteBalance,
        [this, &price](
            OfferFrame const
            & o)
    {
        return
            filterOffer(price,
                o);
    });

    switch (r)
    {
    case OfferExchange::eOK:
    case OfferExchange::ePartial:
        break;
    case OfferExchange::eFilterStop:
        if (innerResult().code() != ManageOfferResultCode::SUCCESS)
        {
            return false;
        }
        break;
    default:
        throw std::runtime_error("Unexpected offer exchange result");
    }

    // updates the result with the offers that got taken on the way
    auto takenOffers = oe.getOfferTrail();

    for (auto const& oatom : takenOffers)
    {
        innerResult().success().offersClaimed.push_back(oatom);
    }

    if (!takenOffers.empty())
    {
        int64_t currentPrice = takenOffers[takenOffers.size() - 1].currentPrice;
        mAssetPair->setCurrentPrice(currentPrice);
        EntryHelperProvider::storeChangeEntry(delta, db, mAssetPair->mEntry);

        EntryHelperProvider::storeChangeEntry(delta, db,
            commissionBalance->mEntry);
    }

    if (oe.offerNeedsMore(offer))
    {
        offerFrame->mEntry.data.offer().offerID = delta.getHeaderFrame().
            generateID(LedgerEntryType
                ::
                OFFER_ENTRY);
        innerResult().success().offer.effect(ManageOfferEffect::CREATED);
        EntryHelperProvider::storeAddEntry(delta, db, offerFrame->mEntry);
        innerResult().success().offer.offer() = offer;
    }
    else
    {
        OfferExchange::unlockBalancesForTakenOffer(*offerFrame, mBaseBalance,
            mQuoteBalance);
        innerResult().success().offer.effect(ManageOfferEffect::DELETED);
    }

    innerResult().success().baseAsset = mAssetPair->getBaseAsset();
    innerResult().success().quoteAsset = mAssetPair->getQuoteAsset();
    EntryHelperProvider::storeChangeEntry(delta, db, mBaseBalance->mEntry);
    EntryHelperProvider::storeChangeEntry(delta, db, mQuoteBalance->mEntry);

    return true;
}

bool ManageOfferOpFrame::lockSellingAmount(OfferEntry const& offer)
{
    BalanceFrame::pointer sellingBalance;
    int64_t sellingAmount;
    if (offer.isBuy)
    {
        sellingBalance = mQuoteBalance;
        sellingAmount = offer.quoteAmount + offer.fee;
    }
    else
    {
        sellingBalance = mBaseBalance;
        sellingAmount = offer.baseAmount;
    }

    if (sellingAmount <= 0)
        return false;
    return sellingBalance->lockBalance(sellingAmount) == BalanceFrame::Result::
           SUCCESS;
}

std::unordered_map<AccountID, CounterpartyDetails> ManageOfferOpFrame::
getCounterpartyDetails(Database& db, LedgerDelta* delta) const
{
    // no counterparties
    return {};
}

SourceDetails ManageOfferOpFrame::getSourceAccountDetails(
    std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails)
const
{
    uint32_t allowedBlockedReasons = 0;
    if (mManageOffer.offerID != 0 && mManageOffer.amount == 0)
        allowedBlockedReasons = getAnyBlockReason();
    return SourceDetails({AccountType::GENERAL, AccountType::NOT_VERIFIED},
                         mSourceAccount->getMediumThreshold(),
                         static_cast<int32_t>(SignerType::BALANCE_MANAGER),
                         allowedBlockedReasons);
}

bool
ManageOfferOpFrame::doApply(Application& app, LedgerDelta& delta,
                            LedgerManager& ledgerManager)
{
    Database& db = ledgerManager.getDatabase();
    // deleting offer
    if (mManageOffer.offerID)
    {
        return deleteOffer(db, delta);
    }

    return createOffer(app, delta, ledgerManager);
}

// makes sure the currencies are different
bool ManageOfferOpFrame::doCheckValid(Application& app)
{
    const bool isPriceInvalid = mManageOffer.amount < 0 || mManageOffer.price <= 0;
    const bool isTryingToUpdate = mManageOffer.offerID > 0 && mManageOffer.amount > 0;
    const bool isDeleting = mManageOffer.amount == 0 && mManageOffer.offerID > 0;
    const bool isQuoteAmountFits = isDeleting || OfferManager::calcualteQuoteAmount(mManageOffer.amount, mManageOffer.price) > 0;
    if (isPriceInvalid || isTryingToUpdate || !isQuoteAmountFits || mManageOffer
        .fee < 0)
    {
        innerResult().code(ManageOfferResultCode::MALFORMED);
        return false;
    }

    if (mManageOffer.baseBalance == mManageOffer.quoteBalance)
    {
        innerResult().code(ManageOfferResultCode::ASSET_PAIR_NOT_TRADABLE);
        return false;
    }

    if (mManageOffer.offerID == 0 && mManageOffer.amount == 0)
    {
        innerResult().code(ManageOfferResultCode::NOT_FOUND);
        return false;
    }

    return true;
}
}
