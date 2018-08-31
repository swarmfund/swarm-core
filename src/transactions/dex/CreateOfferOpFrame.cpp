// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include <ledger/AssetHelper.h>
#include "util/asio.h"
#include "CreateOfferOpFrame.h"
#include "OfferExchange.h"
#include "database/Database.h"
#include "ledger/LedgerDelta.h"
#include "ledger/AccountHelper.h"
#include "ledger/AssetPairHelper.h"
#include "ledger/BalanceHelper.h"
#include "ledger/OfferFrame.h"
#include "main/Application.h"
#include "util/Logging.h"
#include "OfferManager.h"
#include "transactions/FeesManager.h"

namespace stellar
{
using namespace std;
using xdr::operator==;

// TODO requires refactoring
CreateOfferOpFrame::CreateOfferOpFrame(Operation const& op,
                                       OperationResult& res,
                                       TransactionFrame& parentTx)
    : ManageOfferOpFrame(op, res, parentTx)
{
}

BalanceFrame::pointer CreateOfferOpFrame::loadBalanceValidForTrading(
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

AssetPairFrame::pointer CreateOfferOpFrame::loadTradableAssetPair(
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

bool CreateOfferOpFrame::checkOfferValid(Database& db, LedgerDelta& delta)
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

    BalanceID receivingBalance;
    if (mManageOffer.isBuy)
        receivingBalance = mManageOffer.baseBalance;
    else
        receivingBalance = mManageOffer.quoteBalance;

    if (!isAllowedToReceive(receivingBalance, db))
    {
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

OfferExchange::OfferFilterResult CreateOfferOpFrame::filterOffer(const uint64_t price,
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

bool CreateOfferOpFrame::lockSellingAmount(OfferEntry const& offer)
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

FeeManager::FeeResult
CreateOfferOpFrame::obtainCalculatedFeeForAccount(int64_t amount, LedgerManager& lm, Database& db) const
{
    if (lm.shouldUse(LedgerVersion::ADD_CAPITAL_DEPLOYMENT_FEE_TYPE) && isCapitalDeployment)
    {
        return FeeManager::calculateCapitalDeploymentFeeForAccount(mSourceAccount, mQuoteBalance->getAsset(), amount, db);
    }

    return FeeManager::calculateOfferFeeForAccount(mSourceAccount, mQuoteBalance->getAsset(), amount, db);
}

bool
CreateOfferOpFrame::doApply(Application& app, LedgerDelta& delta,
                            LedgerManager& ledgerManager)
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
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected state: quote amount overflows";
        throw std::runtime_error("Unexpected state: quote amount overflows");
    }

    auto& offer = offerFrame->getOffer();
    offer.createdAt = ledgerManager.getCloseTime();
    auto const feeResult = obtainCalculatedFeeForAccount(offer.quoteAmount, ledgerManager, db);

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

    const BalanceFrame::pointer commissionBalance = AccountManager::loadOrCreateBalanceFrameForAsset(app.getCommissionID(), mAssetPair->getQuoteAsset(), db, delta);

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
        const int64_t currentPrice = takenOffers[takenOffers.size() - 1].currentPrice;
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

// makes sure the currencies are different
bool CreateOfferOpFrame::doCheckValid(Application& app)
{
    if (mManageOffer.amount <= 0)
    {
        innerResult().code(ManageOfferResultCode::INVALID_AMOUNT);
        return false;
    }

    if (mManageOffer.price <= 0)
    {
        innerResult().code(ManageOfferResultCode::PRICE_IS_INVALID);
        return false;
    }

    const bool isQuoteAmountFits = OfferManager::calculateQuoteAmount(mManageOffer.amount, mManageOffer.price) > 0;
    if (!isQuoteAmountFits)
    {
        innerResult().code(ManageOfferResultCode::OFFER_OVERFLOW);
        return false;
    }
    if (mManageOffer.fee < 0)
    {
        innerResult().code(ManageOfferResultCode::INVALID_PERCENT_FEE);
        return false;
    }

    if (mManageOffer.baseBalance == mManageOffer.quoteBalance)
    {
        innerResult().code(ManageOfferResultCode::ASSET_PAIR_NOT_TRADABLE);
        return false;
    }

    return true;
}

bool
CreateOfferOpFrame::isAllowedToReceive(BalanceID receivingBalance, Database &db)
{
    const auto result = AccountManager::isAllowedToReceive(receivingBalance, db);
    switch (result){
        case AccountManager::SUCCESS:
            return true;
        case AccountManager::BALANCE_NOT_FOUND:
            innerResult().code(ManageOfferResultCode::BALANCE_NOT_FOUND);
            return false;
        case AccountManager::REQUIRED_VERIFICATION:
            innerResult().code(ManageOfferResultCode::REQUIRES_VERIFICATION);
            return false;
        case AccountManager::REQUIRED_KYC:
            innerResult().code(ManageOfferResultCode::REQUIRES_KYC);
            return false;
        default:
            CLOG(ERROR, Logging::OPERATION_LOGGER)
                    << "Unexpected isAllowedToReceive method result from accountManager:" << result;
            throw std::runtime_error("Unexpected isAllowedToReceive method result from accountManager");
    }
}
}
