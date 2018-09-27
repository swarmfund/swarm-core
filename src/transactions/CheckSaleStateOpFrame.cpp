// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include <transactions/dex/ManageSaleOpFrame.h>
#include "CheckSaleStateOpFrame.h"
#include "ledger/LedgerDelta.h"
#include "database/Database.h"
#include "ledger/SaleHelper.h"
#include "main/Application.h"
#include "dex/ManageOfferOpFrame.h"
#include "ledger/OfferHelper.h"
#include "dex/OfferManager.h"
#include "FeesManager.h"
#include "ledger/AccountHelper.h"
#include "xdrpp/printer.h"
#include "ledger/AssetHelperLegacy.h"
#include "issuance/CreateIssuanceRequestOpFrame.h"
#include "dex/CreateOfferOpFrame.h"
#include "dex/CreateSaleParticipationOpFrame.h"
#include "ledger/BalanceHelperLegacy.h"
#include "ledger/AssetPairHelper.h"
#include "ledger/SaleAnteHelper.h"
#include "dex/DeleteSaleParticipationOpFrame.h"

namespace stellar
{
using namespace std;
using xdr::operator==;


CheckSaleStateOpFrame::SaleState CheckSaleStateOpFrame::getSaleState(
    const SaleFrame::pointer sale, Database& db, LedgerManager& lm)
{
    uint64_t currentCap = 0;
    if (!CreateSaleParticipationOpFrame::getSaleCurrentCap(sale, db, currentCap))
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Failed to calculate current cap for sale: " << sale->getID();
        throw runtime_error("Failed to calculate current cap for sale");
    }

    const auto reachedHardCap = currentCap >= sale->getHardCap();
    if (reachedHardCap)
    {
        return CLOSE;
    }

    const auto currentTime = lm.getCloseTime();
    const auto expired = sale->getEndTime() <= currentTime;
    if (expired)
    {
        const auto reachedSoftCap = currentCap >= sale->getSoftCap();
        if (reachedSoftCap)
        {
            return CLOSE;
        }

        return CANCEL;
    }

    return NOT_READY;
}

unordered_map<AccountID, CounterpartyDetails> CheckSaleStateOpFrame::
getCounterpartyDetails(Database& db, LedgerDelta* delta) const
{
    // no counterparties
    return {};
}

SourceDetails CheckSaleStateOpFrame::getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails>, int32_t ledgerVersion)
const
{
    return SourceDetails({ AccountType::MASTER }, mSourceAccount->getLowThreshold(), int32_t(SignerType::EVENTS_CHECKER));
}

void CheckSaleStateOpFrame::issueBaseTokens(const SaleFrame::pointer sale, const AccountFrame::pointer saleOwnerAccount, Application& app,
    LedgerDelta& delta, Database& db, LedgerManager& lm, TokenAction action) const
{
    AccountManager::unlockPendingIssuanceForSale(sale, delta, db, lm);

    auto result = applyCreateIssuanceRequest(sale, saleOwnerAccount, app, delta, lm);
    if (result.code() != CreateIssuanceRequestResultCode::SUCCESS)
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected state. Create issuance request returned unepected result!"
            << xdr::xdr_traits<CreateIssuanceRequestResultCode>::enum_name(result.code()) << "; saleID: " << sale->getID();
        throw runtime_error("Unexpected state. Create issuance request returned unepected result!");
    }

    if (!result.success().fulfilled)
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected state; issuance request was not fulfilled on check sale state" << "saleID: " << sale->getID();
        throw runtime_error("Unexpected state; issuance request was not fulfilled on check sale state");
    }

    if (action == RESTRICT || (!lm.shouldUse(LedgerVersion::ALLOW_TO_ISSUE_AFTER_SALE)))
    {
        restrictIssuanceAfterSale(sale, delta, db, lm);
    }
    if (action == DESTROY)
    {
        updateAvailableForIssuance(sale, delta, db);
    }
}

bool CheckSaleStateOpFrame::handleCancel(SaleFrame::pointer sale, LedgerManager& lm, LedgerDelta& delta,
    Database& db)
{
    ManageSaleOpFrame::cancelSale(sale, delta, db, lm);

    innerResult().code(CheckSaleStateResultCode::SUCCESS);
    auto& success = innerResult().success();
    success.saleID = sale->getID();
    success.effect.effect(CheckSaleStateEffect::CANCELED);
    return true;
}

void CheckSaleStateOpFrame::chargeSaleAntes(uint64_t saleID, AccountID const &commissionID,
                                            LedgerDelta &delta, Database &db)
{
    auto saleAntes = SaleAnteHelper::Instance()->loadSaleAntesForSale(saleID, db);
    for (auto &saleAnte : saleAntes) {
        auto participantBalanceFrame = BalanceHelperLegacy::Instance()->mustLoadBalance(saleAnte->getParticipantBalanceID(),
                                                                                  db, &delta);
        auto commissionBalance = AccountManager::loadOrCreateBalanceFrameForAsset(commissionID,
                                                                                  participantBalanceFrame->getAsset(),
                                                                                  db, delta);
        if (!commissionBalance) {
            CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected state. Expected commission balance to exist";
            throw std::runtime_error("Unexpected state. Expected commission balance to exist");
        }

        if (!participantBalanceFrame->tryChargeFromLocked(saleAnte->getAmount())) {
            string strParticipantBalanceID = PubKeyUtils::toStrKey(participantBalanceFrame->getBalanceID());
            CLOG(ERROR, Logging::OPERATION_LOGGER)
                    << "Failed to charge from locked amount for sale ante with sale id: " << saleAnte->getSaleID()
                    << " and participant balance id: " << strParticipantBalanceID;
            throw std::runtime_error("Failed to charge from locked amount for sale ante");
        }

        if (!commissionBalance->tryFundAccount(saleAnte->getAmount())) {
            string strCommissionBalanceID = PubKeyUtils::toStrKey(commissionBalance->getBalanceID());
            CLOG(ERROR, Logging::OPERATION_LOGGER)
                    << "Failed to fund commission balance with sale ante - overflow. Sale id: "
                    << saleAnte->getSaleID() << " and commission balance id: " << strCommissionBalanceID;
            throw runtime_error("Failed to fund commission balance with sale ante");
        }

        EntryHelperProvider::storeChangeEntry(delta, db, participantBalanceFrame->mEntry);
        EntryHelperProvider::storeChangeEntry(delta, db, commissionBalance->mEntry);
        EntryHelperProvider::storeDeleteEntry(delta, db, saleAnte->getKey());
    }
}


void CheckSaleStateOpFrame::cleanupIssuerBalance(SaleFrame::pointer sale, LedgerManager& lm, Database& db, LedgerDelta& delta, BalanceFrame::pointer balanceBefore){
    auto balanceAfter = BalanceHelperLegacy::Instance()->loadBalance(sale->getBaseBalanceID(), db);
    if(!lm.shouldUse(LedgerVersion::ALLOW_CLOSE_SALE_WITH_NON_ZERO_BALANCE)) {
        
        if (balanceAfter->getAmount() > ONE)
        {
            CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected state: after sale close issuer endup with balance > ONE: " << sale->getID();
            throw runtime_error("Unexpected state: after sale close issuer endup with balance > ONE");
        }
        return;
    }
    
    auto balanceDelta = safeDelta(balanceBefore->getAmount(), balanceAfter->getAmount());
    if (balanceDelta > ONE) {
        CLOG(ERROR, Logging::OPERATION_LOGGER)
            << "Unexpected state: after sale close issuer endup with balance different from before sale"
            << sale->getID();
        throw runtime_error(
            "Unexpected state: after sale close issuer endup with balance different from before sale");
    }

    if (!lm.shouldUse(LedgerVersion::ALLOW_TO_ISSUE_AFTER_SALE)) {
        return;
    }

    // return delta back to the asset
    if (!balanceAfter->tryCharge(balanceDelta)) {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected state: failed to clean up sale manager balance after sale been performed: " << sale->getID();
        throw std::runtime_error("Unexpected state: failed to clean up sale manager balance after sale been performed");
    }

    BalanceHelperLegacy::Instance()->storeChange(delta, db, balanceAfter->mEntry);

    // return base asset
    auto asset = AssetHelperLegacy::Instance()->mustLoadAsset(balanceAfter->getAsset(), db, &delta);
    if (!asset->tryUnIssue(balanceDelta)) {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected state: failed to unissue redundant amount after sale been performed: "
            << sale->getID();
        throw std::runtime_error("Unexpected state: failed to unissue redundant amount after sale been performed");
    }

    AssetHelperLegacy::Instance()->storeChange(delta, db, asset->mEntry);

    return;
}


bool CheckSaleStateOpFrame::handleClose(SaleFrame::pointer sale, Application& app,
    LedgerManager& lm, LedgerDelta& delta, Database& db)
{
    updateOfferPrices(sale, delta, db);
    const auto saleOwnerAccount = AccountHelper::Instance()->loadAccount(sale->getOwnerID(), db, &delta);
    if (!saleOwnerAccount)
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected db state: expected sale owner to exist: " << PubKeyUtils::toStrKey(sale->getOwnerID());
        throw runtime_error("Unexpected db state: expected sale owner to exist");
    }

    auto balanceBefore = BalanceHelperLegacy::Instance()->loadBalance(sale->getBaseBalanceID(), db);

    issueBaseTokens(sale, saleOwnerAccount, app, delta, db, lm);

    innerResult().code(CheckSaleStateResultCode::SUCCESS);
    auto& success = innerResult().success();
    success.saleID = sale->getID();
    success.effect.effect(CheckSaleStateEffect::CLOSED);
    success.effect.saleClosed().saleOwner = saleOwnerAccount->getID();
    for (auto const& quoteAsset : sale->getSaleEntry().quoteAssets)
    {
        if (quoteAsset.currentCap == 0)
            continue;
        const auto saleOfferResult = applySaleOffer(saleOwnerAccount, sale, quoteAsset, app, lm, delta);
        CheckSubSaleClosedResult result;
        result.saleDetails = saleOfferResult;
        result.saleBaseBalance = sale->getBaseBalanceID();
        result.saleQuoteBalance = quoteAsset.quoteBalance;
        success.effect.saleClosed().results.push_back(result);
        ManageSaleOpFrame::cancelAllOffersForQuoteAsset(sale, quoteAsset, delta, db);
    }

    if (lm.shouldUse(LedgerVersion::USE_SALE_ANTE)) {
        chargeSaleAntes(sale->getID(), app.getCommissionID(), delta, db);
    }

    SaleHelper::Instance()->storeDelete(delta, db, sale->getKey());

    cleanupIssuerBalance(sale, lm, db, delta, balanceBefore);

    return true;
}

CreateIssuanceRequestResult CheckSaleStateOpFrame::applyCreateIssuanceRequest(
    SaleFrame::pointer sale, const AccountFrame::pointer saleOwnerAccount,
    Application& app, LedgerDelta& delta, LedgerManager& lm) const
{
    Database& db = app.getDatabase();
    const auto asset = AssetHelperLegacy::Instance()->loadAsset(sale->getBaseAsset(), db);
    //TODO Must be refactored
    uint64_t amountToIssue = std::min(sale->getBaseAmountForCurrentCap(), asset->getMaxIssuanceAmount());

    const auto issuanceRequestOp = CreateIssuanceRequestOpFrame::build(sale->getBaseAsset(), amountToIssue,
                                                                       sale->getBaseBalanceID(), lm, 0);
    Operation op;
    op.sourceAccount.activate() = sale->getOwnerID();
    op.body.type(OperationType::CREATE_ISSUANCE_REQUEST);
    op.body.createIssuanceRequestOp() = issuanceRequestOp;

    OperationResult opRes;
    opRes.code(OperationResultCode::opINNER);
    opRes.tr().type(OperationType::CREATE_ISSUANCE_REQUEST);
    CreateIssuanceRequestOpFrame createIssuanceRequestOpFrame(op, opRes, mParentTx);
    createIssuanceRequestOpFrame.doNotRequireFee();
    createIssuanceRequestOpFrame.setSourceAccountPtr(saleOwnerAccount);
    if (!createIssuanceRequestOpFrame.doCheckValid(app) || !createIssuanceRequestOpFrame.doApply(app, delta, lm))
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected state: failed to apply create issuance request on check sale: " << sale->getID();
        throw runtime_error("Unexpected state: failed to create issuance request on check sale");
    }

    return createIssuanceRequestOpFrame.getResult().tr().createIssuanceRequestResult();
}

void CheckSaleStateOpFrame::restrictIssuanceAfterSale(SaleFrame::pointer sale, LedgerDelta & delta, Database & db, LedgerManager & lm)
{
    updateAvailableForIssuance(sale, delta, db);

    updateMaxIssuance(sale, delta, db, lm);
}

void CheckSaleStateOpFrame::updateMaxIssuance(const SaleFrame::pointer sale,
    LedgerDelta& delta, Database& db, LedgerManager& lm)
{
    // no need to update max issuance
    if (lm.shouldUse(LedgerVersion::FIX_UPDATE_MAX_ISSUANCE))
    {
        return;
    }

    auto baseAsset = AssetHelperLegacy::Instance()->loadAsset(sale->getBaseAsset(), db, &delta);

    uint64_t updatedMaxIssuance = 0;

    if (lm.shouldUse(LedgerVersion::FIX_SET_SALE_STATE_AND_CHECK_SALE_STATE_OPS))
    {
        uint64_t issued = baseAsset->getIssued();
        uint64_t pendingIssuance = baseAsset->getPendingIssuance();
        if (!safeSum(issued, pendingIssuance, updatedMaxIssuance))
        {
            CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected state: overflow on new max issuance amount calculation: "
                                                      << xdr::xdr_to_string(sale->getSaleEntry());
            throw runtime_error("Unexpected state: overflow on new max issuance amount calculation");
        }
    }
    else
    {
        updatedMaxIssuance = baseAsset->getIssued();
    }

    baseAsset->setMaxIssuance(updatedMaxIssuance);
    AssetHelperLegacy::Instance()->storeChange(delta, db, baseAsset->mEntry);
}

void CheckSaleStateOpFrame::updateAvailableForIssuance(const SaleFrame::pointer sale, LedgerDelta &delta,
                                                       Database &db)
{
    auto baseAsset = AssetHelperLegacy::Instance()->mustLoadAsset(sale->getBaseAsset(), db);

    // destroy remaining assets (difference between hardCap and currentCap)
    baseAsset->setAvailableForIssuance(0);

    EntryHelperProvider::storeChangeEntry(delta, db, baseAsset->mEntry);
}

FeeManager::FeeResult
CheckSaleStateOpFrame::obtainCalculatedFeeForAccount(const AccountFrame::pointer saleOwnerAccount,
                                                     AssetCode const& asset, int64_t amount,
                                                     LedgerManager& lm, Database& db) const
{
    if (lm.shouldUse(LedgerVersion::ADD_CAPITAL_DEPLOYMENT_FEE_TYPE))
    {
        return FeeManager::calculateCapitalDeploymentFeeForAccount(saleOwnerAccount, asset, amount, db);
    }

    return FeeManager::calculateOfferFeeForAccount(saleOwnerAccount, asset, amount, db);
}

ManageOfferSuccessResult CheckSaleStateOpFrame::applySaleOffer(
    const AccountFrame::pointer saleOwnerAccount, SaleFrame::pointer sale, SaleQuoteAsset const& saleQuoteAsset, Application& app,
    LedgerManager& lm, LedgerDelta& delta) const
{
    auto& db = app.getDatabase();
    auto baseBalance = BalanceHelperLegacy::Instance()->mustLoadBalance(sale->getBaseBalanceID(), db);

    uint64_t baseAmount = min(sale->getBaseAmountForCurrentCap(saleQuoteAsset.quoteAsset), static_cast<uint64_t>(baseBalance->getAmount()));
    int64_t quoteAmount = OfferManager::calculateQuoteAmount(baseAmount, saleQuoteAsset.price);
    auto saleType = sale->getSaleType();
    auto baseAsset = sale->getBaseAsset();
    auto price = saleQuoteAsset.price;

    auto const feeResult = obtainCalculatedFeeForAccount(saleOwnerAccount, saleQuoteAsset.quoteAsset, quoteAmount, lm, db);

    if (feeResult.isOverflow)
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected state: overflow on sale fees calculation: " << xdr::xdr_to_string(sale->getSaleEntry());
        throw runtime_error("Unexpected state: overflow on sale fees calculation");
    }


    ManageOfferOp manageOfferOp;
    manageOfferOp = OfferManager::buildManageOfferOp(sale->getBaseBalanceID(), saleQuoteAsset.quoteBalance,
    false, baseAmount, price, feeResult.calculatedPercentFee, 0, sale->getID());

    Operation op;
    op.sourceAccount.activate() = sale->getOwnerID();
    op.body.type(OperationType::MANAGE_OFFER);
    op.body.manageOfferOp() = manageOfferOp;

    OperationResult opRes;
    opRes.code(OperationResultCode::opINNER);
    opRes.tr().type(OperationType::MANAGE_OFFER);
    // need to directly create CreateOfferOpFrame to bypass validation of CreateSaleParticipationOpFrame
    CreateOfferOpFrame manageOfferOpFrame(op, opRes, mParentTx);
    manageOfferOpFrame.isCapitalDeployment = true;
    manageOfferOpFrame.setSourceAccountPtr(saleOwnerAccount);
    if (!manageOfferOpFrame.doCheckValid(app) || !manageOfferOpFrame.doApply(app, delta, lm))
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected state: failed to apply manage offer on check sale: " << sale->getID() << manageOfferOpFrame.getInnerResultCodeAsStr();
        throw runtime_error("Unexpected state: failed to apply manage offer on check sale");
    }

    const auto manageOfferResultCode = ManageOfferOpFrame::getInnerCode(opRes);
    if (manageOfferResultCode != ManageOfferResultCode::SUCCESS)
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected result code from manage offer on check sale state"
            << xdr::xdr_traits<ManageOfferResultCode>::enum_name(manageOfferResultCode);
        throw runtime_error("Unexpected result code from manage offer on check sale state");
    }

    auto result = opRes.tr().manageOfferResult().success();
    if (result.offersClaimed.empty())
    {
        throw runtime_error("Unexpected state: sale was closed, but no order matches");
    }

    return result;
}

bool CheckSaleStateOpFrame::cleanSale(SaleFrame::pointer sale, Application& app,
    LedgerDelta& delta, LedgerManager& ledgerManager) const
{
    if (sale->getSaleType() != SaleType::CROWD_FUNDING)
    {
        return false;
    }

    auto& db = app.getDatabase();
    const auto saleState = getSaleState(sale, db, ledgerManager);
    if (saleState != CLOSE)
    {
        return false;
    }

    bool wasUpdated = false;
    const int64_t priceInDefaultQuoteAsset = getSaleCurrentPriceInDefaultQuote(sale, delta, db);
    for (auto const& quoteAsset : sale->getSaleEntry().quoteAssets)
    {
        const int64_t priceInQuoteAsset = getPriceInQuoteAsset(priceInDefaultQuoteAsset, sale, quoteAsset.quoteAsset, db);
        int64_t minAllowedQuoteAmount = 0;
        if (!bigDivide(minAllowedQuoteAmount, priceInQuoteAsset, 1, ONE, ROUND_UP))
        {
            CLOG(ERROR, Logging::OPERATION_LOGGER) << "Failed to calculate min allowed quote amount: " << sale->getID();
            throw runtime_error("Failed to calculate min quote amount");
        }

        // it's not possible to create offer with quote amount < 1, so we can continue
        if (minAllowedQuoteAmount == 1)
        {
            continue;
        }

        auto offersToCancel = OfferHelper::Instance()->loadOffers(sale->getBaseAsset(), quoteAsset.quoteAsset, sale->getID(), minAllowedQuoteAmount, db);
        for (const auto offerToCancel : offersToCancel)
        {
            DeleteSaleParticipationOpFrame::deleteSaleParticipation(app, delta, ledgerManager, offerToCancel, mParentTx);
            wasUpdated = true;
        }


    }

    return wasUpdated;
}

void CheckSaleStateOpFrame::updateOfferPrices(SaleFrame::pointer sale,
    LedgerDelta& delta, Database& db) const
{
    auto saleType = sale->getSaleType();
    if (saleType != SaleType::CROWD_FUNDING && saleType != SaleType::FIXED_PRICE)
    {
        return;
    }

    auto& saleEntry = sale->getSaleEntry();
    uint64_t priceInDefaultQuoteAsset = getSaleCurrentPriceInDefaultQuote(sale, delta, db);

    for (auto& quoteAsset : saleEntry.quoteAssets)
    {
        quoteAsset.price = getPriceInQuoteAsset(priceInDefaultQuoteAsset, sale, quoteAsset.quoteAsset, db);
        const auto offersToUpdate = OfferHelper::Instance()->loadOffersWithFilters(sale->getBaseAsset(), quoteAsset.quoteAsset, &saleEntry.saleID, nullptr, db);
        for (auto& offerToUpdate : offersToUpdate)
        {
            auto& offerEntry = offerToUpdate->getOffer();
            offerEntry.price = quoteAsset.price;

            if (!bigDivide(offerEntry.baseAmount, offerEntry.quoteAmount, ONE, offerEntry.price, ROUND_DOWN))
            {
                CLOG(ERROR, Logging::OPERATION_LOGGER) << "Failed to update price for offer: offerID: " << offerEntry.offerID;
                throw runtime_error("Failed to update price for offer on check state");
            }

                        OfferHelper::Instance()->storeChange(delta, db, offerToUpdate->mEntry);
        }
    }

    SaleHelper::Instance()->storeChange(delta, db, sale->mEntry);
}

int64_t CheckSaleStateOpFrame::getSaleCurrentPriceInDefaultQuote(
    const SaleFrame::pointer sale, LedgerDelta& delta, Database& db)
{

    if (sale->getSaleType() == SaleType::FIXED_PRICE)
    {
        uint64_t priceInDefaultQuoteAsset = 0;
        if (!bigDivide(priceInDefaultQuoteAsset, sale->getHardCap(), ONE, sale->getMaxAmountToBeSold(), ROUND_UP))
        {
            CLOG(ERROR, Logging::OPERATION_LOGGER) << "Failed to update prices! saleID: " << sale->getID();
            throw runtime_error("Failed to update prices");
        }

        return priceInDefaultQuoteAsset;
    }

    uint64_t currentCap = 0;
    if (!CreateSaleParticipationOpFrame::getSaleCurrentCap(sale, db, currentCap) || currentCap > INT64_MAX)
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Failed to get sale current cap! saleID: " << sale->getID();
        throw runtime_error("Failed to get current sale cap");
    }

    return getSalePriceForCap(currentCap, sale);
}

int64_t CheckSaleStateOpFrame::getPriceInQuoteAsset(
    int64_t const salePriceInDefaultQuote, const SaleFrame::pointer sale,
    AssetCode const quoteAsset, Database& db)
{
    if (sale->getDefaultQuoteAsset() == quoteAsset)
    {
        return salePriceInDefaultQuote;
    }

    auto assetPair = AssetPairHelper::Instance()->tryLoadAssetPairForAssets(sale->getDefaultQuoteAsset(), quoteAsset, db);
    if (!assetPair)
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Failed to load asset pair for quote asset and default quote asset. SaleID: " 
            << sale->getID() << " quoteAsset" << quoteAsset;
        throw runtime_error("failed to load default quote asset for sale");
    }

    int64_t priceInQuoteAsset = 0;
    auto ok = false;
    if (assetPair->getBaseAsset() == sale->getDefaultQuoteAsset())
    {
        ok = bigDivide(priceInQuoteAsset, salePriceInDefaultQuote, assetPair->getCurrentPrice(), ONE, ROUND_UP);
    } else
    {
        ok = bigDivide(priceInQuoteAsset, salePriceInDefaultQuote, ONE, assetPair->getCurrentPrice(), ROUND_UP);
    }

    if (!ok)
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Failed to calculate price in quote asset. SaleID: "
            << sale->getID() << " quoteAsset" << quoteAsset;
        throw runtime_error("Failed to calculate price in quote asset");
    }

    return priceInQuoteAsset;
}

CheckSaleStateOpFrame::CheckSaleStateOpFrame(Operation const& op,
    OperationResult& res, TransactionFrame& parentTx) : OperationFrame(op, res, parentTx), mCheckSaleState(mOperation.body.checkSaleStateOp())
{
}

bool CheckSaleStateOpFrame::doApply(Application& app, LedgerDelta& delta,
    LedgerManager& ledgerManager)
{
    auto& db = app.getDatabase();
    const auto sale = SaleHelper::Instance()->loadSale(mCheckSaleState.saleID, db, &delta);
    if (!sale)
    {
        innerResult().code(CheckSaleStateResultCode::NOT_FOUND);
        return false;
    }

    const auto wasCleaned = cleanSale(sale, app, delta, ledgerManager);
    if (wasCleaned)
    {
        innerResult().code(CheckSaleStateResultCode::SUCCESS);
        innerResult().success().saleID = sale->getID();
        innerResult().success().effect.effect(CheckSaleStateEffect::UPDATED);
        return true;
    }
    auto const  saleState = getSaleState(sale, db, ledgerManager);
    switch (saleState)
    {
    case CLOSE:
        return handleClose(sale, app, ledgerManager, delta, db);
    case CANCEL:
        return handleCancel(sale, ledgerManager, delta, db);
    case NOT_READY:
        innerResult().code(CheckSaleStateResultCode::NOT_READY);
        return false;
    default:
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected sale state: " << saleState;
        throw runtime_error("Unexpected sale state");
    }
}

bool CheckSaleStateOpFrame::doCheckValid(Application& app)
{
    return true;
}

int64_t CheckSaleStateOpFrame::getSalePriceForCap(int64_t const cap,
                                                  const SaleFrame::pointer sale)
{
    int64_t currentSalePrice = 0;
    if (!bigDivide(currentSalePrice, cap, ONE, sale->getMaxAmountToBeSold(), ROUND_UP))
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Failed to calculate current sale price in default quote. SaleID: " << sale->getID();
        throw runtime_error("Failed to calculate current sale price in default quote");
    }

    return currentSalePrice;
}

std::string CheckSaleStateOpFrame::getInnerResultCodeAsStr()
{
    const auto code = getInnerCode(mResult);
    return xdr::xdr_traits<CheckSaleStateResultCode>::enum_name(code);
}
}
