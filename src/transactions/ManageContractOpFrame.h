#pragma once

// Copyright 2015 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "transactions/OperationFrame.h"
#include "ledger/ReviewableRequestFrame.h"
#include "ledger/ContractFrame.h"

namespace stellar
{
class ManageContractOpFrame : public OperationFrame
{
    std::unordered_map<AccountID, CounterpartyDetails> getCounterpartyDetails(Database& db,
                                                                              LedgerDelta* delta) const override;
    SourceDetails getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
                                          int32_t ledgerVersion) const override;

    ManageContractResult&
    innerResult()
    {
        return mResult.tr().manageContractResult();
    }

    ManageContractOp const& mManageContract;

    bool checkIsAllowed(ContractFrame::pointer contractFrame);

    bool checkContractDetails(ContractFrame::pointer contractFrame, Application& app,
                              Database& db, LedgerDelta& delta);

    uint64_t obtainMaxContractDetailsCount(Application& app, Database& db, LedgerDelta& delta);

    bool confirmCompleted(ContractFrame::pointer contractFrame, Database& db, LedgerDelta& delta);

    bool checkIsCompleted(ContractFrame::pointer contractFrame,
                          std::vector<ReviewableRequestFrame::pointer> invoiceRequests,
                          Database& db, LedgerDelta& delta);

    bool checkInvoices(std::vector<ReviewableRequestFrame::pointer> invoiceRequests);

    bool startDispute(ContractFrame::pointer contractFrame,
                      Application& app, Database& db, LedgerDelta& delta);

    bool resolveDispute(ContractFrame::pointer contractFrame, Database& db, LedgerDelta& delta);

    bool revertInvoicesAmounts(ContractFrame::pointer contractFrame, Database& db, LedgerDelta& delta);

    void unlockApprovedInvoicesAmounts(ContractFrame::pointer contractFrame, Database& db, LedgerDelta & delta);

public:
    ManageContractOpFrame(Operation const& op, OperationResult& res,
                           TransactionFrame& parentTx);

    static uint64_t obtainMaxContractDetailLength(Application& app, Database& db, LedgerDelta& delta);

    bool doApply(Application& app, LedgerDelta& delta,
                 LedgerManager& ledgerManager) override;
    bool doCheckValid(Application& app) override;

    static ManageContractResultCode
    getInnerCode(OperationResult const& res)
    {
        return res.tr().manageContractResult().code();
    }

    std::string getInnerResultCodeAsStr() override;
};
}
