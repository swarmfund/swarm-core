#pragma once

// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ledger/LedgerManager.h"
#include "SaleHelper.h"

namespace soci
{
    class session;
}

namespace stellar
{
    class StatementContext;

    class SaleQuoteAssetHelper {
    public:
        static void dropAll(Database& db);
        static void deleteAllForSale(Database& db, uint64_t saleID);
        static void storeUpdate(Database & db, uint64_t const saleID, xdr::xvector<SaleQuoteAsset, 100> quoteAssets, bool insert);
        static void storeUpdate(Database & db, uint64_t const saleID, SaleQuoteAsset const& quoteAsset, bool insert);
        static void loadSaleQuoteAsset(StatementContext& prep, const std::function<void(SaleQuoteAsset const&)> saleProcessor);
        static xdr::xvector<SaleQuoteAsset, 100> loadQuoteAssets(Database& db, uint64_t saleID);
    };
}