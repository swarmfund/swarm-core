// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "DeleteSaleParticipationOpFrame.h"
#include "ledger/LedgerDelta.h"
#include "database/Database.h"
#include "ledger/SaleHelper.h"
#include "main/Application.h"
#include "ledger/OfferHelper.h"
#include "CreateSaleParticipationOpFrame.h"
#include "ledger/BalanceHelperLegacy.h"
#include "ledger/AccountHelper.h"

namespace stellar
{
using namespace std;
using xdr::operator==;

DeleteSaleParticipationOpFrame::DeleteSaleParticipationOpFrame(
    Operation const& op, OperationResult& res, TransactionFrame& parentTx) : DeleteOfferOpFrame(op, res, parentTx)
{
    mCheckSaleState = true;
}

bool DeleteSaleParticipationOpFrame::doCheckValid(Application& app)
{
    if (mManageOffer.orderBookID == SECONDARY_MARKET_ORDER_BOOK_ID)
    {
        throw invalid_argument("Delete sale participation: unexpected order book id SECONDARY_MARKET_ORDER_BOOK_ID");
    }

    if (!mManageOffer.isBuy)
    {
        innerResult().code(ManageOfferResultCode::MALFORMED);
        return false;
    }

    return DeleteOfferOpFrame::doCheckValid(app);
}

bool DeleteSaleParticipationOpFrame::doApply(Application& app,
    LedgerDelta& delta, LedgerManager& ledgerManager)
{
    auto& db = app.getDatabase();
    auto offer = OfferHelper::Instance()->loadOffer(getSourceID(), mManageOffer.offerID, mManageOffer.orderBookID, db,
        &delta);
    if (!offer)
    {
        innerResult().code(ManageOfferResultCode::NOT_FOUND);
        return false;
    }

    auto sale = SaleHelper::Instance()->loadSale(mManageOffer.orderBookID, db, &delta);
    if (!sale)
    {
        innerResult().code(ManageOfferResultCode::ORDER_BOOK_DOES_NOT_EXISTS);
        return false;
    }

    if (mCheckSaleState && CreateSaleParticipationOpFrame::getSaleState(sale, db, ledgerManager.getCloseTime()) != SaleFrame::State::ACTIVE)
    {
        innerResult().code(ManageOfferResultCode::SALE_IS_NOT_ACTIVE);
        return false;
    }

    auto quoteBalanceID = getQuoteBalanceID(offer, ledgerManager);
    auto balance = BalanceHelperLegacy::Instance()->mustLoadBalance(quoteBalanceID, db);
    sale->subCurrentCap(balance->getAsset(), offer->getOffer().quoteAmount);
    sale->unlockBaseAsset(offer->getOffer().baseAmount);
    SaleHelper::Instance()->storeChange(delta, db, sale->mEntry);
    return DeleteOfferOpFrame::doApply(app, delta, ledgerManager);
}

void DeleteSaleParticipationOpFrame::deleteSaleParticipation(
    Application& app, LedgerDelta& delta, LedgerManager& ledgerManager,
    OfferFrame::pointer offer, TransactionFrame& parentTx)
{
    Database& db = app.getDatabase();
    Operation op;
    auto& offerEntry = offer->getOffer();
    op.sourceAccount.activate() = offerEntry.ownerID;
    op.body.type(OperationType::MANAGE_OFFER);
    auto& manageOfferOp = op.body.manageOfferOp();
    manageOfferOp.quoteBalance = offerEntry.quoteBalance;
    manageOfferOp.amount = 0;
    manageOfferOp.baseBalance = offerEntry.baseBalance;
    manageOfferOp.fee = offerEntry.fee;
    manageOfferOp.isBuy = offerEntry.isBuy;
    manageOfferOp.offerID = offerEntry.offerID;
    manageOfferOp.orderBookID = offerEntry.orderBookID;
    manageOfferOp.price = offerEntry.price;

    OperationResult opRes;
    opRes.code(OperationResultCode::opINNER);
    opRes.tr().type(OperationType::MANAGE_OFFER);
    DeleteSaleParticipationOpFrame opFrame(op, opRes, parentTx);
    opFrame.doNotCheckSaleState();
    const auto offerOwner = AccountHelper::Instance()->mustLoadAccount(offerEntry.ownerID, db);
    opFrame.setSourceAccountPtr(offerOwner);
    if (!opFrame.doCheckValid(app) || !opFrame.doApply(app, delta, ledgerManager))
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected state: failed to apply delete sale participation: offerID" << offer->getOfferID();
        throw runtime_error("Unexpected state: failed to apply delete sale participation");
    }
}

void DeleteSaleParticipationOpFrame::doNotCheckSaleState()
{
    mCheckSaleState = false;
}
BalanceID DeleteSaleParticipationOpFrame::getQuoteBalanceID(OfferFrame::pointer offer, LedgerManager& lm)
{
    if (!lm.shouldUse(LedgerVersion::ALLOW_TO_CANCEL_SALE_PARTICIP_WITHOUT_SPECIFING_BALANCE)) {
        return mManageOffer.quoteBalance;
    }

    return offer->getOffer().quoteBalance;
}
}
