// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "CreateSaleParticipationOpFrame.h"
#include "ledger/LedgerDelta.h"
#include "database/Database.h"
#include "ledger/SaleHelper.h"
#include "main/Application.h"
#include "ledger/OfferHelper.h"
#include "OfferManager.h"
#include "ledger/AccountHelper.h"
#include "ledger/AssetPairHelper.h"
#include "ledger/BalanceHelperLegacy.h"
#include "ledger/FeeHelper.h"
#include "ledger/SaleAnteFrame.h"
#include "ledger/SaleAnteHelper.h"
#include "transactions/CheckSaleStateOpFrame.h"
#include "xdrpp/printer.h"

namespace stellar
{
using namespace std;
using xdr::operator==;


SaleFrame::pointer CreateSaleParticipationOpFrame::loadSaleForOffer(
    Database& db, LedgerDelta& delta)
{
    auto baseBalance = loadBalanceValidForTrading(mManageOffer.baseBalance, db,
                                                  delta);
    if (!baseBalance)
    {
        return nullptr;
    }

    auto quoteBalance = loadBalanceValidForTrading(mManageOffer.quoteBalance,
                                                   db, delta);
    if (!quoteBalance)
    {
        return nullptr;
    }

    auto sale = SaleHelper::Instance()->loadSale(mManageOffer.orderBookID,
                                                 baseBalance->getAsset(),
                                                 quoteBalance->getAsset(), db,
                                                 &delta);
    if (!sale)
    {
        innerResult().code(ManageOfferResultCode::ORDER_BOOK_DOES_NOT_EXISTS);
        return nullptr;
    }

    if (!isPriceValid(sale, quoteBalance, db))
    {
        return nullptr;
    }

    return sale;
}

bool CreateSaleParticipationOpFrame::isPriceValid(SaleFrame::pointer sale, BalanceFrame::pointer quoteBalance, Database& db) const
{
    if (sale->getPrice(quoteBalance->getAsset()) != mManageOffer.price)
    {
        innerResult().code(ManageOfferResultCode::PRICE_DOES_NOT_MATCH);
        return false;
    }

    //ensure that on soft cap we are able to receive some tokens
    if (sale->getSaleType() != SaleType::CROWD_FUNDING)
    {
        return true;
    }

    const int64_t priceForSoftCap = CheckSaleStateOpFrame::getSalePriceForCap(sale->getSoftCap(), sale);
    const int64_t priceInQuoteAsset = CheckSaleStateOpFrame::getPriceInQuoteAsset(priceForSoftCap, sale, quoteBalance->getAsset(), db);
    int64_t baseAmount = 0;
    if (!bigDivide(baseAmount, mManageOffer.amount, ONE, priceInQuoteAsset, ROUND_DOWN))
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Failed to calculate base amount for sale participation on soft cap " << xdr::xdr_to_string(mManageOffer);
        throw runtime_error("Failed to calculate base amount for sale participation on soft cap");
    }

    if (baseAmount == 0)
    {
        innerResult().code(ManageOfferResultCode::INVALID_AMOUNT);
        return false;
    }

    return true;
}

CreateSaleParticipationOpFrame::CreateSaleParticipationOpFrame(
    Operation const& op, OperationResult& res,
    TransactionFrame& parentTx) : CreateOfferOpFrame(op, res, parentTx)
{
}

bool CreateSaleParticipationOpFrame::doCheckValid(Application& app)
{
    if (!mManageOffer.isBuy)
    {
        innerResult().code(ManageOfferResultCode::MALFORMED);
        return false;
    }

    return CreateOfferOpFrame::doCheckValid(app);
}

bool CreateSaleParticipationOpFrame::isSaleActive(Database& db, LedgerManager& ledgerManager, const SaleFrame::pointer sale) const
{
    const auto saleState = getSaleState(sale, db, ledgerManager.getCloseTime());
    switch (saleState)
    {
    case SaleFrame::State::ACTIVE:
        return true;
    case SaleFrame::State::VOTING:
        // just fall through
    case SaleFrame::State::PROMOTION:
        // just fall through
    case SaleFrame::State::NOT_STARTED_YET:
    {
        innerResult().code(ManageOfferResultCode::SALE_IS_NOT_STARTED_YET);
        return false;
    }
    case SaleFrame::State::ENDED:
    {
        innerResult().code(ManageOfferResultCode::SALE_ALREADY_ENDED);
        return false;
    }
    default:
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) <<
            "Unexpected state of the sale: " << static_cast<int32_t>(saleState);
        throw runtime_error("Unexpected state of the sale");
    }
    }
}

bool
CreateSaleParticipationOpFrame::tryCreateSaleAnte(Database &db, LedgerDelta &delta, LedgerManager &ledgerManager,
                                                  BalanceFrame::pointer sourceBalanceFrame, uint64_t saleID)
{
    auto sourceAccountFrame = AccountHelper::Instance()->mustLoadAccount(getSourceID(), db);
    auto investFeeFrame = FeeHelper::Instance()->loadForAccount(FeeType::INVEST_FEE, sourceBalanceFrame->getAsset(), 0,
                                                                sourceAccountFrame, mManageOffer.amount, db);
    if (!investFeeFrame) {
        return true;
    }

    int64_t quoteAssetAmount = 0; // required for calculating amount of sale ante
    if (!bigDivide(quoteAssetAmount, mManageOffer.amount, mManageOffer.price, ONE, ROUND_UP)) {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Failed to calculate quote asset amount - overflow, asset code: "
                                               << investFeeFrame->getAsset();
        throw std::runtime_error("Failed to calculate quote asset amount - overflow");
    }

    uint64_t amountToLock = 0;
    if (!investFeeFrame->calculatePercentFee(static_cast<uint64_t>(quoteAssetAmount), amountToLock, ROUND_UP)) {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Failed to calculate invest percent fee - overflow, asset code: "
                                               << investFeeFrame->getAsset();
        throw std::runtime_error("Failed to calculate invest percent fee - overflow");
    }

    if (!safeSum(amountToLock, static_cast<uint64_t>(investFeeFrame->getFixedFee()), amountToLock)) {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Failed to calculate sale ante amount - overflow, asset code: "
                                               << investFeeFrame->getAsset();
        throw std::runtime_error("Failed to calculate sale ante amount - overflow");
    }

    auto prevSaleAnte = SaleAnteHelper::Instance()->loadSaleAnte(saleID, sourceBalanceFrame->getBalanceID(), db, &delta);
    if (!prevSaleAnte) {
        auto saleAnte = SaleAnteFrame::createNew(saleID, sourceBalanceFrame->getBalanceID(), amountToLock);

        auto lockingResult = sourceBalanceFrame->tryLock(amountToLock);
        if (lockingResult != BalanceFrame::Result::SUCCESS) {
            setErrorCode(lockingResult);
            return false;
        }

        EntryHelperProvider::storeChangeEntry(delta, db, sourceBalanceFrame->mEntry);
        EntryHelperProvider::storeAddEntry(delta, db, saleAnte->mEntry);
        return true;
    }

    if (prevSaleAnte->getAmount() >= amountToLock) {
        return true;
    }

    auto lockingResult = sourceBalanceFrame->tryLock(amountToLock - prevSaleAnte->getAmount());
    if (lockingResult != BalanceFrame::Result::SUCCESS) {
        setErrorCode(lockingResult);
        return false;
    }

    prevSaleAnte->getSaleAnteEntry().amount = amountToLock;

    EntryHelperProvider::storeChangeEntry(delta, db, sourceBalanceFrame->mEntry);
    EntryHelperProvider::storeChangeEntry(delta, db, prevSaleAnte->mEntry);
    return true;
}

void CreateSaleParticipationOpFrame::setErrorCode(BalanceFrame::Result lockingResult)
{
    switch (lockingResult) {
        case BalanceFrame::Result::UNDERFUNDED: {
            innerResult().code(ManageOfferResultCode::SOURCE_UNDERFUNDED);
            return;
        }
        case BalanceFrame::Result::LINE_FULL: {
            innerResult().code(ManageOfferResultCode::SOURCE_BALANCE_LOCK_OVERFLOW);
            return;
        }
        default:
            CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected result code from try lock: " << lockingResult;
            throw std::runtime_error("Unexpected result code from try lock");
    }
}

bool CreateSaleParticipationOpFrame::doApply(Application& app,
                                             LedgerDelta& delta,
                                             LedgerManager& ledgerManager)
{
    auto& db = app.getDatabase();
    auto sale = loadSaleForOffer(db, delta);
    if (!sale)
    {
        return false;
    }

    if (sale->getSaleEntry().ownerID == getSourceID())
    {
        innerResult().code(ManageOfferResultCode::CANT_PARTICIPATE_OWN_SALE);
        return false;
    }

    if (!isSaleActive(db, ledgerManager, sale))
    {
        return false;
    }

    if (!sale->tryLockBaseAsset(mManageOffer.amount))
    {
        innerResult().code(ManageOfferResultCode::ORDER_VIOLATES_HARD_CAP);
        return false;
    }

    const auto quoteAmount = OfferManager::
    calculateQuoteAmount(mManageOffer.amount, mManageOffer.price);
    if (quoteAmount == 0)
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected state: quote amount overflows";
        throw runtime_error("Unexpected state: quote amount overflows");
    }

    auto quoteBalance = BalanceHelperLegacy::Instance()->mustLoadBalance(mManageOffer.quoteBalance, db);
    if (!tryAddSaleCap(db, quoteAmount, quoteBalance->getAsset(), sale))
    {
        innerResult().code(ManageOfferResultCode::ORDER_VIOLATES_HARD_CAP);
        return false;
    }

    if (ledgerManager.shouldUse(LedgerVersion::USE_SALE_ANTE)) {
        if (!tryCreateSaleAnte(db, delta, ledgerManager, quoteBalance, sale->getID())) {
            return false;
        }
    }

    const auto isApplied = CreateOfferOpFrame::doApply(app, delta, ledgerManager);
    if (!isApplied)
    {
        return false;
    }

    if (innerResult().code() != ManageOfferResultCode::SUCCESS)
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) <<
            "Unexpected state: expected success for manage offer on create sale participation, but got: "
            << getInnerResultCodeAsStr();
        throw
            runtime_error("Unexpected state: expected success for manage offer on create sale participation");
    }

    if (!innerResult().success().offersClaimed.empty())
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) <<
            "Unexpected state. Order match on sale participation: " <<
            mManageOffer.orderBookID;
        throw runtime_error("Order match on sale participation");
    }

    SaleHelper::Instance()->storeChange(delta, db, sale->mEntry);

    return true;
}

bool CreateSaleParticipationOpFrame::getSaleCurrentCap(const SaleFrame::pointer sale,
    Database& db, uint64_t& totalCurrentCapInDefaultQuote)
{

    totalCurrentCapInDefaultQuote = 0;
    auto const& saleEntry = sale->getSaleEntry();
    for (auto const& quoteAsset : saleEntry.quoteAssets)
    {
        if (quoteAsset.currentCap == 0)
            continue;

        if (quoteAsset.quoteAsset == saleEntry.defaultQuoteAsset)
        {
            if (!safeSum(totalCurrentCapInDefaultQuote, quoteAsset.currentCap, totalCurrentCapInDefaultQuote))
                return false;

            continue;
        }

        const auto assetPair = AssetPairHelper::Instance()->tryLoadAssetPairForAssets(quoteAsset.quoteAsset, saleEntry.defaultQuoteAsset, db);
        if (!assetPair)
        {
            CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected state: failed to load asset pair for sale: " << saleEntry.saleID
                << " assets: " << quoteAsset.quoteAsset << " & " << saleEntry.defaultQuoteAsset;
            throw runtime_error("Failed to load asset pair for sale");
        }

        uint64_t currentCapInDefaultQuote = 0;
        if (!assetPair->convertAmount(saleEntry.defaultQuoteAsset, quoteAsset.currentCap, ROUND_UP, currentCapInDefaultQuote))
        {
            return false;
        }

        if (!safeSum(totalCurrentCapInDefaultQuote, currentCapInDefaultQuote, totalCurrentCapInDefaultQuote))
            return false;
    }

    return true;
}

SaleFrame::State CreateSaleParticipationOpFrame::getSaleState(const SaleFrame::pointer sale, Database& db, const uint64_t currentTime)
{

    switch (sale->getState()) {
    case SaleState::NONE:
        break;
    case SaleState::VOTING:
        return SaleFrame::State::VOTING;
    case SaleState::PROMOTION:
        return SaleFrame::State::PROMOTION;
    default:
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected sale state; Failed to get Sale State for sale participation: saleID: " << sale->getID();
        throw std::runtime_error("Unexpected sale state; Failed to get Sale State for sale participation");
    }
    uint64_t currentCap = 0;
    if (!getSaleCurrentCap(sale, db, currentCap))
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Failed to calculate current cap for sale: " << sale->getID();
        throw runtime_error("Failed to calculate current cap for sale");
    }

    if (currentCap >= sale->getHardCap() || sale->getEndTime() <= currentTime)
    {
        return SaleFrame::State::ENDED;
    }

    if (sale->getStartTime() > currentTime)
    {
        return SaleFrame::State::NOT_STARTED_YET;
    }

    return SaleFrame::State::ACTIVE;
}

bool CreateSaleParticipationOpFrame::tryAddSaleCap(Database& db, uint64_t const& amount,
    AssetCode const& asset, SaleFrame::pointer sale)
{
    auto& saleQuoteAsset = sale->getSaleQuoteAsset(asset);
    if (!safeSum(amount, saleQuoteAsset.currentCap, saleQuoteAsset.currentCap))
    {
        return false;
    }

    uint64_t currentCap = 0;
    if (!getSaleCurrentCap(sale, db, currentCap))
    {
        return false;
    }

    if (sale->getHardCap() < currentCap)
    {
        const auto isViolationTolerable = currentCap - sale->getHardCap() < ONE;
        if (!isViolationTolerable)
            return false;
    }

    return true;

}
}
