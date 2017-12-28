#pragma once

// Copyright 2015 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "transactions/OperationFrame.h"
#include "ledger/SaleFrame.h"

namespace stellar
{
class CheckSaleStateOpFrame : public OperationFrame
{
    CheckSaleStateResult& innerResult()
    {
        return mResult.tr().checkSaleStateResult();
    }

    CheckSaleStateOp const& mCheckSaleState;

    std::unordered_map<AccountID, CounterpartyDetails> getCounterpartyDetails(
        Database& db, LedgerDelta* delta) const override;

    SourceDetails getSourceAccountDetails(
        std::unordered_map<AccountID, CounterpartyDetails>
        counterpartiesDetails) const override;

    void issueBaseTokens(SaleFrame::pointer sale, AccountFrame::pointer saleOwnerAccount, Application& app, LedgerDelta& delta, Database& db, LedgerManager& lm);

    bool handleCancel(SaleFrame::pointer sale, LedgerManager& lm, LedgerDelta& delta, Database& db);
    bool handleClose(SaleFrame::pointer sale, Application& app, LedgerManager& lm, LedgerDelta& delta, Database& db);


public:

    CheckSaleStateOpFrame(Operation const& op, OperationResult& res,
                         TransactionFrame& parentTx);

    bool doApply(Application& app, LedgerDelta& delta,
                 LedgerManager& ledgerManager) override;
    bool doCheckValid(Application& app) override;

    static CheckSaleStateResultCode getInnerCode(OperationResult const& res)
    {
        return res.tr().checkSaleStateResult().code();
    }

    std::string getInnerResultCodeAsStr() override;
};
}
