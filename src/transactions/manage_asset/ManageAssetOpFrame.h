#pragma once

// Copyright 2015 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "transactions/OperationFrame.h"
#include "ledger/ReviewableRequestFrame.h"

namespace stellar
{

class ManageAssetOpFrame : public OperationFrame
{
protected:
    ManageAssetResult&
    innerResult()
    {
        return mResult.tr().manageAssetResult();
    }
    ManageAssetOp const& mManageAsset;

	std::unordered_map<AccountID, CounterpartyDetails> getCounterpartyDetails(Database& db, LedgerDelta* delta) const override;
	SourceDetails getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
                                              int32_t ledgerVersion) const override;

	// Creates new request if requestID is 0, otherwise tries to load it from db
	ReviewableRequestFrame::pointer getOrCreateReviewableRequest(Application& app, Database& db, LedgerDelta& delta, ReviewableRequestType requestType) const;

        virtual std::string getAssetCode() const = 0;

public:
    
    ManageAssetOpFrame(Operation const& op, OperationResult& res,
                         TransactionFrame& parentTx);

    static ManageAssetResultCode
    getInnerCode(OperationResult const& res)
    {
        return res.tr().manageAssetResult().code();
    }

	static ManageAssetOpFrame* makeHelper(Operation const& op, OperationResult& res, TransactionFrame& parentTx);

	std::string getInnerResultCodeAsStr() override {
		return xdr::xdr_traits<ManageAssetResultCode>::enum_name(innerResult().code());
	}
};
}
