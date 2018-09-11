#pragma once

#include "OperationFrame.h"

namespace stellar
{

class CreateManageLimitsRequestOpFrame : public OperationFrame
{
    CreateManageLimitsRequestResult& innerResult()
    {
        return mResult.tr().createManageLimitsRequestResult();
    }

    CreateManageLimitsRequestOp const& mCreateManageLimitsRequest;

    std::unordered_map<AccountID, CounterpartyDetails> getCounterpartyDetails(Database& db,
                                                                              LedgerDelta* delta) const override;

    SourceDetails getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
                                          int32_t ledgerVersion) const override;

    std::string getLimitsManageRequestReference(Hash const& documentHash) const;
    std::string getLimitsManageRequestDetailsReference(longstring const& details) const;

public:

    CreateManageLimitsRequestOpFrame(Operation const& op, OperationResult& res, TransactionFrame& parentTx);

    bool createManageLimitsRequest(Application& app, LedgerDelta& delta, LedgerManager& ledgerManager);

    bool updateManageLimitsRequest(LedgerManager& lm, Database& db, LedgerDelta& delta);

    bool doApply(Application& app, LedgerDelta& delta, LedgerManager& ledgerManager) override;

    bool doCheckValid(Application& app) override;

    static CreateManageLimitsRequestResultCode getInnerCode(OperationResult const& res)
    {
        return res.tr().createManageLimitsRequestResult().code();
    }

    std::string getInnerResultCodeAsStr() override
    {
        return xdr::xdr_traits<CreateManageLimitsRequestResultCode>::enum_name(innerResult().code());
    }
};

}