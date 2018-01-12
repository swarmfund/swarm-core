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

    if (sale->getPrice() != mManageOffer.price)
    {
        innerResult().code(ManageOfferResultCode::PRICE_DOES_NOT_MATCH);
        return nullptr;
    }

    return sale;
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

bool CreateSaleParticipationOpFrame::isSaleActive(LedgerManager& ledgerManager, const SaleFrame::pointer sale)
{
    const auto saleState = sale->getState(ledgerManager.getCloseTime());
    switch (saleState)
    {
    case SaleFrame::State::ACTIVE:
        return true;
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

    if (!isSaleActive(ledgerManager, sale))
    {
        return false;
    }

    const auto quoteAmount = OfferManager::
        calcualteQuoteAmount(mManageOffer.amount, mManageOffer.price);
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

    const auto isApplied = CreateOfferOpFrame::doApply(app, delta,
                                                       ledgerManager);
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
}
