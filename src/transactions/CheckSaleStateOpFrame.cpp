// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include <transactions/dex/ManageSaleHelper.h>
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
#include "ledger/AssetHelper.h"
#include "issuance/CreateIssuanceRequestOpFrame.h"
#include "dex/CreateOfferOpFrame.h"
#include "dex/CreateSaleParticipationOpFrame.h"
#include "ledger/BalanceHelper.h"

namespace stellar
{
using namespace std;
using xdr::operator==;


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
    LedgerDelta& delta, Database& db, LedgerManager& lm) const
{
    AccountManager::unlockPendingIssuance(sale->getBaseAsset(), delta, db);

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

    updateAvailableForIssuance(sale, delta, db);

    updateMaxIssuance(sale, delta, db);
}

bool CheckSaleStateOpFrame::handleCancel(SaleFrame::pointer sale, LedgerManager& lm, LedgerDelta& delta,
    Database& db)
{
    ManageSaleHelper::cancelSale(sale, delta, db);

    innerResult().code(CheckSaleStateResultCode::SUCCESS);
    auto& success = innerResult().success();
    success.saleID = sale->getID();
    success.effect.effect(CheckSaleStateEffect::CANCELED);
    return true;
}

bool CheckSaleStateOpFrame::handleClose(SaleFrame::pointer sale, Application& app,
    LedgerManager& lm, LedgerDelta& delta, Database& db)
{
    const auto saleOwnerAccount = AccountHelper::Instance()->loadAccount(sale->getOwnerID(), db, &delta);
    if (!saleOwnerAccount)
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected db state: expected sale owner to exist: " << PubKeyUtils::toStrKey(sale->getOwnerID());
        throw runtime_error("Unexpected db state: expected sale owner to exist");
    }

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
        ManageSaleHelper::cancelAllOffersForQuoteAsset(sale, quoteAsset, delta, db);
    }
    SaleHelper::Instance()->storeDelete(delta, db, sale->getKey());

    auto baseBalance = BalanceHelper::Instance()->loadBalance(sale->getBaseBalanceID(), db);
    if (baseBalance->getAmount() > ONE)
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected state: after sale close issuer endup with balance > ONE: " << sale->getID();
        throw runtime_error("Unexpected state: after sale close issuer endup with balance > ONE");
    }
    
    return true;
}

    CreateIssuanceRequestResult CheckSaleStateOpFrame::applyCreateIssuanceRequest(
    SaleFrame::pointer sale, const AccountFrame::pointer saleOwnerAccount,
    Application& app, LedgerDelta& delta, LedgerManager& lm) const
{
    Database& db = app.getDatabase();
    auto asset = AssetHelper::Instance()->loadAsset(sale->getBaseAsset(), db);
    const auto amountToIssue = std::min(sale->getBaseAmountForCurrentCap(), asset->getMaxIssuanceAmount());
    const auto issuanceRequestOp = CreateIssuanceRequestOpFrame::build(sale->getBaseAsset(), amountToIssue, sale->getBaseBalanceID(), lm);
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

void CheckSaleStateOpFrame::updateMaxIssuance(const SaleFrame::pointer sale,
    LedgerDelta& delta, Database& db) const
{
    auto baseAsset = AssetHelper::Instance()->loadAsset(sale->getBaseAsset(), db, &delta);

    // at this point issued amount is the sum of a previously issued amount and a total amount of the sale
    uint64_t updatedMaxIssuance = baseAsset->getIssued();

    baseAsset->setMaxIssuance(updatedMaxIssuance);
    AssetHelper::Instance()->storeChange(delta, db, baseAsset->mEntry);
}

void CheckSaleStateOpFrame::updateAvailableForIssuance(const SaleFrame::pointer sale, LedgerDelta &delta,
                                                       Database &db) const
{
    auto baseAsset = AssetHelper::Instance()->mustLoadAsset(sale->getBaseAsset(), db);

    // destroy remaining assets (difference between hardCap and currentCap)
    baseAsset->setAvailableForIssuance(0);

    EntryHelperProvider::storeChangeEntry(delta, db, baseAsset->mEntry);
}

ManageOfferSuccessResult CheckSaleStateOpFrame::applySaleOffer(
    const AccountFrame::pointer saleOwnerAccount, SaleFrame::pointer sale, SaleQuoteAsset const& saleQuoteAsset, Application& app,
    LedgerManager& lm, LedgerDelta& delta)
{
    auto& db = app.getDatabase();
    auto baseBalance = BalanceHelper::Instance()->mustLoadBalance(sale->getBaseBalanceID(), db);

    const auto baseAmount = min(sale->getBaseAmountForCurrentCap(saleQuoteAsset.quoteAsset), static_cast<uint64_t>(baseBalance->getAmount()));
    const auto quoteAmount = OfferManager::calculateQuoteAmount(baseAmount, saleQuoteAsset.price);
    const auto feeResult = FeeManager::calculateOfferFeeForAccount(saleOwnerAccount, saleQuoteAsset.quoteAsset, quoteAmount, db);
    if (feeResult.isOverflow)
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected state: overflow on sale fees calculation: " << xdr::xdr_to_string(sale->getSaleEntry());
        throw runtime_error("Unexpected state: overflow on sale fees calculation");
    }

    const auto manageOfferOp = OfferManager::buildManageOfferOp(sale->getBaseBalanceID(), saleQuoteAsset.quoteBalance,
        false, baseAmount, saleQuoteAsset.price, feeResult.calculatedPercentFee, 0, sale->getID());
    Operation op;
    op.sourceAccount.activate() = sale->getOwnerID();
    op.body.type(OperationType::MANAGE_OFFER);
    op.body.manageOfferOp() = manageOfferOp;

    OperationResult opRes;
    opRes.code(OperationResultCode::opINNER);
    opRes.tr().type(OperationType::MANAGE_OFFER);
    // need to directly create CreateOfferOpFrame to bypass validation of CreateSaleParticipationOpFrame
    CreateOfferOpFrame manageOfferOpFrame(op, opRes, mParentTx);
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

    uint64_t currentCap = 0;
    if (!CreateSaleParticipationOpFrame::getSaleCurrentCap(sale, db, currentCap))
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Failed to calculate current cap for sale: " << sale->getID();
        throw runtime_error("Failed to calculate current cap for sale");
    }

    const auto currentTime = ledgerManager.getCloseTime();
    const auto expired = sale->getEndTime() <= currentTime;
    const auto reachedSoftCapAndExpired = currentCap >= sale->getSoftCap() && expired;
    const auto reachedHardCap = currentCap >= sale->getHardCap();
    if (reachedHardCap || reachedSoftCapAndExpired)
    {
        return handleClose(sale, app, ledgerManager, delta, db);
    }

    if (expired)
    {
        return handleCancel(sale, ledgerManager, delta, db);
    }

    innerResult().code(CheckSaleStateResultCode::NOT_READY);
    return false;
}

bool CheckSaleStateOpFrame::doCheckValid(Application& app)
{
    return true;
}

std::string CheckSaleStateOpFrame::getInnerResultCodeAsStr()
{
    const auto code = getInnerCode(mResult);
    return xdr::xdr_traits<CheckSaleStateResultCode>::enum_name(code);
}
}
