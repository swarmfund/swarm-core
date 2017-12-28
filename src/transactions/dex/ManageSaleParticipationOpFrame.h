#pragma once

// Copyright 2015 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ManageOfferOpFrame.h"
#include "ledger/SaleFrame.h"

namespace stellar
{
class ManageSaleParticipationOpFrame : public ManageOfferOpFrame
{
    SaleFrame::pointer loadSaleForOffer(Database& db, LedgerDelta& delta);
    bool isSaleActive(LedgerManager& ledgerManager, SaleFrame::pointer sale);
public:

    ManageSaleParticipationOpFrame(Operation const& op, OperationResult& res,
                         TransactionFrame& parentTx);

    bool doCheckValid(Application& app) override;
protected:
    bool deleteOffer(Database& db, LedgerDelta& delta) override;
    bool createOffer(Application& app, LedgerDelta& delta,
        LedgerManager& ledgerManager) override;
};
}
