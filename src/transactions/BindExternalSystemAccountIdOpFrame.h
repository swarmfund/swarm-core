#pragma once

#include "transactions/OperationFrame.h"

namespace stellar
{

class BindExternalSystemAccountIdOpFrame : public OperationFrame
{
    BindExternalSystemAccountIdResult&
    innerResult()
    {
        return mResult.tr().bindExternalSystemAccountIdResult();
    }
    BindExternalSystemAccountIdOp const& mBindExternalSystemAccountId;

    std::unordered_map<AccountID, CounterpartyDetails> getCounterpartyDetails(Database& db, LedgerDelta* delta) const override;
    SourceDetails getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
                                          int32_t ledgerVersion) const override;
    int getExpiresAt(StorageHelper& storageHelper, LedgerManager &ledgerManager, int32 externalSystemType);

public:
    BindExternalSystemAccountIdOpFrame(Operation const& op, OperationResult& res,
                                       TransactionFrame& parentTx);

    bool doApply(Application& app, StorageHelper& storageHelper, LedgerManager& ledgerManager) override;
    bool doCheckValid(Application& app) override;

    static BindExternalSystemAccountIdResultCode
    getInnerCode(OperationResult const& res)
    {
        return res.tr().bindExternalSystemAccountIdResult().code();
    }

    std::string getInnerResultCodeAsStr() override {
        return xdr::xdr_traits<BindExternalSystemAccountIdResultCode>::enum_name(innerResult().code());
    }

    static const uint64_t dayInSeconds = 24 * 60 * 60;
};
}