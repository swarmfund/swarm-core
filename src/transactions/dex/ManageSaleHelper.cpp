//
// Created by volodymyr on 05.02.18.
//

#include <ledger/OfferHelper.h>
#include "ManageSaleHelper.h"
#include "OfferManager.h"

namespace stellar
{

void ManageSaleHelper::cancelSale(stellar::SaleFrame::pointer sale, stellar::LedgerDelta &delta,
                                  stellar::Database &db)
{
    for (auto &saleQuoteAsset : sale->getSaleEntry().quoteAssets) {
        cancelAllOffersForQuoteAsset(sale, saleQuoteAsset, delta, db);
    }

    AccountManager::unlockPendingIssuance(sale->getBaseAsset(), delta, db);

    const auto key = sale->getKey();
    SaleHelper::Instance()->storeDelete(delta, db, key);
}

void ManageSaleHelper::cancelAllOffersForQuoteAsset(SaleFrame::pointer sale, SaleQuoteAsset const &saleQuoteAsset,
                                                    LedgerDelta &delta, Database &db)
{
    auto orderBookID = sale->getID();
    const auto offersToCancel = OfferHelper::Instance()->loadOffersWithFilters(sale->getBaseAsset(),
                                                                               saleQuoteAsset.quoteAsset,
                                                                               &orderBookID, nullptr, db);
    OfferManager::deleteOffers(offersToCancel, db, delta);
}

}