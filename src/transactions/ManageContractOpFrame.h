#pragma once

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

    bool ensureIsAllowed(std::vector<AccountID> validSources);

    bool tryAddContractDetails(ContractFrame::pointer contractFrame, Application &app,
                               Database &db, LedgerDelta &delta);

    uint64_t obtainMaxContractDetailLength(Application& app, Database& db, LedgerDelta& delta);

    bool tryConfirmCompleted(ContractFrame::pointer contractFrame, Database &db, LedgerDelta &delta);

    bool tryCompleted(ContractFrame::pointer contractFrame,
                      std::vector<ReviewableRequestFrame::pointer> invoiceRequests,
                      Database &db, LedgerDelta &delta);

    bool checkIsInvoicesApproved(std::vector<ReviewableRequestFrame::pointer> invoiceRequests);

    bool tryStartDispute(ContractFrame::pointer contractFrame,
                         Application &app, Database &db, LedgerDelta &delta);

    bool tryResolveDispute(ContractFrame::pointer contractFrame, Database &db, LedgerDelta &delta);

    bool revertInvoicesAmounts(ContractFrame::pointer contractFrame, Database& db, LedgerDelta& delta);

    void unlockApprovedInvoicesAmounts(ContractFrame::pointer contractFrame, Database& db, LedgerDelta & delta);
public:

    ManageContractOpFrame(Operation const& op, OperationResult& res,
                           TransactionFrame& parentTx);

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
