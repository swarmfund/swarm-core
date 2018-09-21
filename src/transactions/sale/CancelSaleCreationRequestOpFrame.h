#pragma once

#include "transactions/OperationFrame.h"
#include "ledger/ReviewableRequestFrame.h"

namespace stellar
{
class CancelSaleCreationRequestOpFrame : public OperationFrame
{
    CancelSaleCreationRequestResult& innerResult()
    {
        return mResult.tr().cancelSaleCreationRequestResult();
    }

    CancelSaleCreationRequestOp const& mCancelSaleCreationRequest;

    std::unordered_map<AccountID, CounterpartyDetails> getCounterpartyDetails(
                    Database& db, LedgerDelta* delta) const override;

    SourceDetails getSourceAccountDetails(
       std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
       int32_t ledgerVersion) const override;

public:

    CancelSaleCreationRequestOpFrame(Operation const& op, OperationResult& res,
                                     TransactionFrame& parentTx);
    bool doApply(Application& app, LedgerDelta& delta,
                 LedgerManager& ledgerManager) override;

    bool doCheckValid(Application& app) override;

    static CancelSaleCreationRequestResultCode getInnerCode(
            OperationResult const& res)
    {
        return res.tr().cancelSaleCreationRequestResult().code();
    }

    std::string getInnerResultCodeAsStr() override
    {
        return xdr::xdr_traits<CancelSaleCreationRequestResultCode>::
                enum_name(innerResult().code());
    }
};
}
