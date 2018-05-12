#pragma once

#include "transactions/OperationFrame.h"

namespace stellar
{

class ManageExternalSystemAccountIdPoolEntryOpFrame : public OperationFrame
{
protected:
    ManageExternalSystemAccountIdPoolEntryResult&
    innerResult()
    {
        return mResult.tr().manageExternalSystemAccountIdPoolEntryResult();
    }
    ManageExternalSystemAccountIdPoolEntryOp const& mManageExternalSystemAccountIdPoolEntryOp;

    std::unordered_map<AccountID, CounterpartyDetails> getCounterpartyDetails(Database& db,
                                                                              LedgerDelta* delta) const override;
    SourceDetails getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
                                          int32_t ledgerVersion) const override;
public:
    ManageExternalSystemAccountIdPoolEntryOpFrame(Operation const& op, OperationResult& res,
                                        TransactionFrame& parentTx);

    static ManageExternalSystemAccountIdPoolEntryResultCode getInnerCode(OperationResult const& res)
    {
        return res.tr().manageExternalSystemAccountIdPoolEntryResult().code();
    }

    static ManageExternalSystemAccountIdPoolEntryOpFrame* makeHelper(Operation const& op, OperationResult& res,
                                                                     TransactionFrame& parentTx);

    std::string getInnerResultCodeAsStr() override {
        return xdr::xdr_traits<ManageExternalSystemAccountIdPoolEntryResultCode>::enum_name(innerResult().code());
    }
};
}
