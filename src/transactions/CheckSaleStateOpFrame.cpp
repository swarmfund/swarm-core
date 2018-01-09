// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

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
#include "ledger/BalanceHelper.h"
#include "issuance/CreateIssuanceRequestOpFrame.h"
#include "dex/CreateOfferOpFrame.h"

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

SourceDetails CheckSaleStateOpFrame::getSourceAccountDetails(unordered_map<AccountID, CounterpartyDetails>)
const
{
    return SourceDetails({ AccountType::MASTER }, mSourceAccount->getLowThreshold(), int32_t(SignerType::EVENTS_CHECKER));
}

void CheckSaleStateOpFrame::issueBaseTokens(const SaleFrame::pointer sale, const AccountFrame::pointer saleOwnerAccount, Application& app,
    LedgerDelta& delta, Database& db, LedgerManager& lm) const
{
    unlockPendingIssunace(sale, delta, db);

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

    updateMaxIssuance(sale, delta, db);
}

bool CheckSaleStateOpFrame::handleCancel(const SaleFrame::pointer sale, LedgerManager& lm, LedgerDelta& delta,
    Database& db)
{
    auto orderBookID = sale->getID();
    const auto offersToCancel = OfferHelper::Instance()->loadOffersWithFilters(sale->getBaseAsset(), sale->getQuoteAsset(), &orderBookID, nullptr, db);
    OfferManager::deleteOffers(offersToCancel, db, delta);

    unlockPendingIssunace(sale, delta, db);

    const auto key = sale->getKey();
    SaleHelper::Instance()->storeDelete(delta, db, key);
    innerResult().code(CheckSaleStateResultCode::SUCCESS);
    auto& success = innerResult().success();
    success.saleID = sale->getID();
    success.effect.effect(CheckSaleStateEffect::CANCELED);
    return true;
}

bool CheckSaleStateOpFrame::handleClose(const SaleFrame::pointer sale, Application& app,
    LedgerManager& lm, LedgerDelta& delta, Database& db)
{
    const auto saleOwnerAccount = AccountHelper::Instance()->loadAccount(sale->getOwnerID(), db, &delta);
    if (!saleOwnerAccount)
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected db state: expected sale owner to exist: " << PubKeyUtils::toStrKey(sale->getOwnerID());
        throw runtime_error("Unexpected db state: expected sale owner to exist");
    }

    issueBaseTokens(sale, saleOwnerAccount, app, delta, db, lm);

    const auto saleOfferResult = applySaleOffer(saleOwnerAccount, sale, app, lm, delta);
    SaleHelper::Instance()->storeDelete(delta, db, sale->getKey());
    innerResult().code(CheckSaleStateResultCode::SUCCESS);
    auto& success = innerResult().success();
    success.saleID = sale->getID();
    success.effect.effect(CheckSaleStateEffect::CLOSED);
    success.effect.saleClosed().saleDetails = saleOfferResult;
    success.effect.saleClosed().saleOwner = saleOwnerAccount->getID();
    success.effect.saleClosed().saleBaseBalance = sale->getBaseBalanceID();
    success.effect.saleClosed().saleQuoteBalance = sale->getQuoteBalanceID();
    return true;
}

void CheckSaleStateOpFrame::unlockPendingIssunace(const SaleFrame::pointer sale,
    LedgerDelta& delta, Database& db) const
{
    auto baseAsset = AssetHelper::Instance()->mustLoadAsset(sale->getBaseAsset(), db, &delta);
    const auto baseAmount = sale->getBaseAmountForCurrentCap();
    baseAsset->mustUnlockIssuedAmount(baseAmount);
    AssetHelper::Instance()->storeChange(delta, db, baseAsset->mEntry);
}

CreateIssuanceRequestResult CheckSaleStateOpFrame::applyCreateIssuanceRequest(
    const SaleFrame::pointer sale, const AccountFrame::pointer saleOwnerAccount,
    Application& app, LedgerDelta& delta, LedgerManager& lm) const
{
    const auto issuanceRequestOp = CreateIssuanceRequestOpFrame::build(sale->getBaseAsset(), sale->getBaseAmountForCurrentCap(), sale->getBaseBalanceID(), lm);
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
    uint64_t updatedMaxIssuance;
    if (!safeSum(baseAsset->getIssued(), baseAsset->getPendingIssuance(), updatedMaxIssuance))
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Failed to calculate updated max issuance on sale check state; saleID: " << sale->getID();
        throw std::runtime_error("Failed to calculate updated max issuance on sale check state");
    }

    baseAsset->setMaxIssuance(updatedMaxIssuance);
    AssetHelper::Instance()->storeChange(delta, db, baseAsset->mEntry);
}

ManageOfferSuccessResult CheckSaleStateOpFrame::applySaleOffer(
    const AccountFrame::pointer saleOwnerAccount, SaleFrame::pointer sale, Application& app,
    LedgerManager& lm, LedgerDelta& delta)
{
    auto& db = app.getDatabase();
    const auto baseAmount = sale->getBaseAmountForCurrentCap();
    const auto quoteAmount = OfferManager::calcualteQuoteAmount(baseAmount, sale->getPrice());
    const auto feeResult = FeeManager::calculateOfferFeeForAccount(saleOwnerAccount, sale->getQuoteAsset(), quoteAmount, db);
    if (feeResult.isOverflow)
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected state: overflow on sale fees calculation: " << xdr::xdr_to_string(sale->getSaleEntry());
        throw runtime_error("Unexpected state: overflow on sale fees calculation");
    }

    const auto manageOfferOp = OfferManager::buildManageOfferOp(sale->getBaseBalanceID(), sale->getQuoteBalanceID(),
        false, baseAmount, sale->getPrice(), feeResult.calculatedPercentFee, 0, sale->getID());
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

    return opRes.tr().manageOfferResult().success();
}

CheckSaleStateOpFrame::CheckSaleStateOpFrame(Operation const& op,
    OperationResult& res, TransactionFrame& parentTx) : OperationFrame(op, res, parentTx), mCheckSaleState(mOperation.body.checkSaleStateOp())
{
}

bool CheckSaleStateOpFrame::doApply(Application& app, LedgerDelta& delta,
    LedgerManager& ledgerManager)
{
    auto& db = app.getDatabase();
    const auto sale = SaleHelper::Instance()->loadRequireStateChange(ledgerManager.getCloseTime(), db, delta);
    if (!sale)
    {
        innerResult().code(CheckSaleStateResultCode::NO_SALES_FOUND);
        return false;
    }

    if (sale->getSoftCap() < sale->getCurrentCap())
    {
        return handleClose(sale, app, ledgerManager, delta, db);
    }

    return handleCancel(sale, ledgerManager, delta, db);
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
