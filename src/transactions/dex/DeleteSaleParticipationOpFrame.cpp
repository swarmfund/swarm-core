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
#include "ledger/BalanceHelper.h"

namespace stellar
{
using namespace std;
using xdr::operator==;

DeleteSaleParticipationOpFrame::DeleteSaleParticipationOpFrame(
    Operation const& op, OperationResult& res, TransactionFrame& parentTx) : DeleteOfferOpFrame(op, res, parentTx)
{
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

    if (CreateSaleParticipationOpFrame::getSaleState(sale, db, ledgerManager.getCloseTime()) != SaleFrame::State::ACTIVE)
    {
        innerResult().code(ManageOfferResultCode::SALE_IS_NOT_ACTIVE);
        return false;
    }

    auto balance = BalanceHelper::Instance()->mustLoadBalance(mManageOffer.quoteBalance, db);
    sale->subCurrentCap(balance->getAsset(), offer->getOffer().quoteAmount);
    SaleHelper::Instance()->storeChange(delta, db, sale->mEntry);
    return DeleteOfferOpFrame::doApply(app, delta, ledgerManager);
}

}
