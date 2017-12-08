#pragma once

// Copyright 2015 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "transactions/OperationFrame.h"
#include "ledger/AssetFrame.h"
#include "ledger/ReviewableRequestFrame.h"
#include "transactions/review_request/ReviewIssuanceCreationRequestOpFrame.h"

namespace stellar
{
class CreateIssuanceRequestOpFrame : public OperationFrame
{
    CreateIssuanceRequestResult&
    innerResult()
    {
        return mResult.tr().createIssuanceRequestResult();
    }

	CreateIssuanceRequestOp const& mCreateIssuanceRequest;
	
	std::unordered_map<AccountID, CounterpartyDetails> getCounterpartyDetails(Database& db, LedgerDelta* delta) const override;
	SourceDetails getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails) const override;

	bool isAuthorizedToRequestIssuance(AssetFrame::pointer assetFrame);

	// returns nullptr and sets error code if failed to create request
	ReviewableRequestFrame::pointer tryCreateIssuanceRequest(Application& app, LedgerDelta& delta,
		LedgerManager& ledgerManager);

public:

    CreateIssuanceRequestOpFrame(Operation const& op, OperationResult& res,
                       TransactionFrame& parentTx);

    bool doApply(Application& app, LedgerDelta& delta,
                 LedgerManager& ledgerManager) override;
    bool doCheckValid(Application& app) override;

    static CreateIssuanceRequestResultCode
    getInnerCode(OperationResult const& res)
    {
        return res.tr().createIssuanceRequestResult().code();
    }

	std::string getInnerResultCodeAsStr() override {
		return xdr::xdr_traits<CreateIssuanceRequestResultCode>::enum_name(innerResult().code());
	}
    
};
}
