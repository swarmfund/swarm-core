#pragma once

//
// Created by volodymyr on 04.02.18.
//

#include "xdr/Stellar-operation-manage-sale.h"
#include <transactions/OperationFrame.h>

#include "main/Application.h"
#include "medida/metrics_registry.h"

namespace stellar
{

class ManageSaleOpFrame : public OperationFrame
{
protected:
    ManageSaleOp const& mManageSaleOp;

    ManageSaleResult& innerResult() {
        return mResult.tr().manageSaleResult();
    }

    std::unordered_map<AccountID, CounterpartyDetails> getCounterpartyDetails(Database& db, LedgerDelta* delta) const override;

    SourceDetails getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
                                          int32_t ledgerVersion) const override;

public:
    ManageSaleOpFrame(Operation const& op, OperationResult& opRes, TransactionFrame& parentTx);

    bool doCheckValid(Application &app) override;

    bool doApply(Application &app, LedgerDelta &delta, LedgerManager &ledgerManager) override;

    static ManageSaleResultCode getInnerCode(OperationResult& res)
    {
        return res.tr().manageSaleResult().code();
    }
};

}

