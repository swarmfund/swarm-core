#pragma once

// Copyright 2015 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "transactions/OperationFrame.h"
#include "ledger/ReviewableRequestFrame.h"

namespace stellar
{
class CreateSaleCreationRequestOpFrame : public OperationFrame
{
    CreateSaleCreationRequestResult& innerResult()
    {
        return mResult.tr().createSaleCreationRequestResult();
    }

    CreateSaleCreationRequestOp const& mCreateSaleCreationRequest;

    std::unordered_map<AccountID, CounterpartyDetails> getCounterpartyDetails(
        Database& db, LedgerDelta* delta) const override;
    SourceDetails getSourceAccountDetails(
        std::unordered_map<AccountID, CounterpartyDetails>
        counterpartiesDetails) const override;

    
    bool isBaseAssetOrCreationRequestExists(SaleCreationRequest const& request, Database& db) const;

    std::string getReference(SaleCreationRequest const& request) const;

    ReviewableRequestFrame::pointer createNewUpdateRequest(Application& app, Database& db, LedgerDelta& delta, time_t closedAt);

public:

    CreateSaleCreationRequestOpFrame(Operation const& op, OperationResult& res,
                                   TransactionFrame& parentTx);
    bool doApply(Application& app, LedgerDelta& delta,
                 LedgerManager& ledgerManager) override;

    bool doCheckValid(Application& app) override;

    static CreateSaleCreationRequestResultCode getInnerCode(
        OperationResult const& res)
    {
        return res.tr().createSaleCreationRequestResult().code();
    }

    std::string getInnerResultCodeAsStr() override {
        return xdr::xdr_traits<CreateSaleCreationRequestResultCode>::enum_name(innerResult().code());
    }
};
}
