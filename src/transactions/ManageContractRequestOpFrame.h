#pragma once

#include <xdr/Stellar-operation-manage-invoice-request.h>
#include "transactions/OperationFrame.h"

namespace stellar
{
class ManageContractRequestOpFrame : public OperationFrame
{

    ManageContractRequestResult&
    innerResult()
    {
        return mResult.tr().manageContractRequestResult();
    }

    ManageContractRequestOp const& mManageContractRequest;

    std::unordered_map<AccountID, CounterpartyDetails> getCounterpartyDetails(Database& db,
                                                                              LedgerDelta* delta) const override;
    SourceDetails getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
                                          int32_t ledgerVersion) const override;

    bool createManageContractRequest(Application& app, LedgerDelta& delta, LedgerManager& ledgerManager);

    bool checkMaxContractsForContractor(Application& app, Database& db, LedgerDelta& delta);

    uint64_t obtainMaxContractsForContractor(Application& app, Database& db, LedgerDelta& delta);

    bool checkMaxContractDetailLength(Application& app, Database& db, LedgerDelta& delta);

    uint64_t obtainMaxContractInitialDetailLength(Application& app, Database& db, LedgerDelta& delta);

public:
    ManageContractRequestOpFrame(Operation const& op, OperationResult& res, TransactionFrame& parentTx);

    bool doApply(Application& app, LedgerDelta& delta, LedgerManager& ledgerManager) override;
    bool doCheckValid(Application& app) override;

    static ManageContractRequestResultCode
    getInnerCode(OperationResult const& res)
    {
        return res.tr().manageContractRequestResult().code();
    }
};
}
