// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ManageSaleParticipationOpFrame.h"
#include "ledger/LedgerDelta.h"
#include "database/Database.h"
#include "ledger/SaleHelper.h"
#include "main/Application.h"
#include "ledger/OfferHelper.h"
#include "OfferManager.h"

namespace stellar
{
using namespace std;
using xdr::operator==;


SaleFrame::pointer ManageSaleParticipationOpFrame::loadSaleForOffer(
    Database& db, LedgerDelta& delta)
{
    auto baseBalance = loadBalanceValidForTrading(mManageOffer.baseBalance, db, delta);
    if (!baseBalance)
    {
        return nullptr;
    }

    auto quoteBalance = loadBalanceValidForTrading(mManageOffer.quoteBalance, db, delta);
    if (!quoteBalance)
    {
        return nullptr;
    }

    auto sale = SaleHelper::Instance()->loadSale(mManageOffer.orderBookID, baseBalance->getAsset(), quoteBalance->getAsset(), db, &delta);
    if (!sale)
    {
        innerResult().code(ManageOfferResultCode::ORDER_BOOK_DOES_NOT_EXISTS);
        return nullptr;
    }

    if (sale->getPrice() != mManageOffer.price)
    {
        innerResult().code(ManageOfferResultCode::PRICE_DOES_NOT_MATCH);
        return nullptr;
    }

    return sale;
}

bool ManageSaleParticipationOpFrame::isSaleActive(LedgerManager& ledgerManager, SaleFrame::pointer sale)
{
    if (sale->getStartTime() > ledgerManager.getCloseTime())
    {
        innerResult().code(ManageOfferResultCode::SALE_IS_NOT_STARTED_YET);
        return false;
    }

    if (sale->getEndTime() < ledgerManager.getCloseTime())
    {
        innerResult().code(ManageOfferResultCode::SALE_ALREADY_ENDED);
        return false;
    }

    return true;
}

ManageSaleParticipationOpFrame::ManageSaleParticipationOpFrame(
    Operation const& op, OperationResult& res, TransactionFrame& parentTx) : ManageOfferOpFrame(op, res, parentTx)
{
}

bool ManageSaleParticipationOpFrame::doCheckValid(Application& app)
{
    if (!mManageOffer.isBuy)
    {
        innerResult().code(ManageOfferResultCode::MALFORMED);
        return false;
    }

    return ManageOfferOpFrame::doCheckValid(app);
}

bool ManageSaleParticipationOpFrame::deleteOffer(Database& db,
    LedgerDelta& delta)
{
    auto sale = loadSaleForOffer(db, delta);
    if (!sale)
    {
        return false;
    }

    auto offer = OfferHelper::Instance()->loadOffer(getSourceID(), mManageOffer.offerID, db,
        &delta);
    if (!offer)
    {
        innerResult().code(ManageOfferResultCode::NOT_FOUND);
        return false;
    }

    sale->subCurrentCap(offer->getOffer().quoteAmount);
    return ManageOfferOpFrame::deleteOffer(db, delta);
}

bool ManageSaleParticipationOpFrame::createOffer(Application& app,
    LedgerDelta& delta, LedgerManager& ledgerManager)
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

    if (!isSaleActive(ledgerManager, sale))
    {
        return false;
    }

    const auto quoteAmount = OfferManager::calcualteQuoteAmount(mManageOffer.amount, mManageOffer.price);
    if (quoteAmount == 0)
    {
        innerResult().code(ManageOfferResultCode::MALFORMED);
        return false;
    }

    if (!sale->tryAddCap(quoteAmount))
    {
        innerResult().code(ManageOfferResultCode::ORDER_VIOLATES_HARD_CAP);
        return false;
    }

    const auto isApplied = ManageOfferOpFrame::createOffer(app, delta, ledgerManager);
    if (!isApplied)
    {
        return false;
    }

    if (!innerResult().success().offersClaimed.empty())
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected state. Order match on sale participation: " << mManageOffer.orderBookID;
        throw runtime_error("Order match on sale participation");
    }

    SaleHelper::Instance()->storeChange(delta, db, sale->mEntry);

    return true;
}
}
