#pragma once

// Copyright 2015 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ledger/SaleFrame.h"
#include "DeleteOfferOpFrame.h"

namespace stellar
{
class DeleteSaleParticipationOpFrame : public DeleteOfferOpFrame
{

    bool mCheckSaleState;
public:

    DeleteSaleParticipationOpFrame(Operation const& op, OperationResult& res,
                         TransactionFrame& parentTx);

    bool doCheckValid(Application& app) override;

    bool doApply(Application& app, LedgerDelta& delta,
        LedgerManager& ledgerManager) override;

    static void deleteSaleParticipation(Application& app, LedgerDelta& delta,
        LedgerManager& ledgerManager, OfferFrame::pointer offer, TransactionFrame& parentTx);

    void doNotCheckSaleState();

    BalanceID getQuoteBalanceID(OfferFrame::pointer offer, LedgerManager& lm);
};
}
