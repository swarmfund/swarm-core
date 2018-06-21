#pragma once

// Copyright 2015 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ledger/SaleFrame.h"
#include "CreateOfferOpFrame.h"

namespace stellar
{
class CreateSaleParticipationOpFrame : public CreateOfferOpFrame
{
    SaleFrame::pointer loadSaleForOffer(Database& db, LedgerDelta& delta);

    bool isPriceValid(SaleFrame::pointer sale, BalanceFrame::pointer balance, Database& db) const;

    bool tryCreateSaleAnte(Database& db, LedgerDelta& delta, LedgerManager& ledgerManager,
                           BalanceFrame::pointer sourceBalanceFrame, uint64_t saleID);

    void setErrorCode(BalanceFrame::Result lockingResult);

public:

    CreateSaleParticipationOpFrame(Operation const& op, OperationResult& res,
                         TransactionFrame& parentTx);

    bool doCheckValid(Application& app) override;
    bool isSaleActive(Database& db,LedgerManager& ledgerManager, SaleFrame::pointer sale) const;
    bool doApply(Application& app, LedgerDelta& delta,
        LedgerManager& ledgerManager) override;

    static bool getSaleCurrentCap(SaleFrame::pointer const sale, Database& db, uint64_t& currentCapInDefaultQuote);

    static SaleFrame::State getSaleState(SaleFrame::pointer const sale, Database& db, const uint64_t currentTime);
    static bool tryAddSaleCap(Database& db, uint64_t const& amount, AssetCode const& asset, SaleFrame::pointer sale);
};
}
