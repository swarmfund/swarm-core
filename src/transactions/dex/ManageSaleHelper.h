#pragma once

//
// Created by volodymyr on 05.02.18.
//
#include <ledger/SaleFrame.h>
#include <transactions/AccountManager.h>
#include <ledger/SaleHelper.h>

namespace stellar
{

class ManageSaleHelper
{
public:

    static void cancelSale(SaleFrame::pointer sale, LedgerDelta& delta, Database& db);

    static void cancelAllOffersForQuoteAsset(SaleFrame::pointer sale, SaleQuoteAsset const& saleQuoteAsset,
                                             LedgerDelta& delta, Database& db);

};


}

