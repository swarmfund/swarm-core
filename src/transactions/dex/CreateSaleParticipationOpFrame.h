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
public:

    CreateSaleParticipationOpFrame(Operation const& op, OperationResult& res,
                         TransactionFrame& parentTx);

    bool doCheckValid(Application& app) override;
    bool isSaleActive(LedgerManager& ledgerManager, SaleFrame::pointer sale);
    bool doApply(Application& app, LedgerDelta& delta,
        LedgerManager& ledgerManager) override;
};
}
