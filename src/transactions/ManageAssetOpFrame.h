#pragma once

// Copyright 2015 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "transactions/OperationFrame.h"

namespace stellar
{

class ManageAssetOpFrame : public OperationFrame
{
    ManageAssetResult&
    innerResult()
    {
        return mResult.tr().manageAssetResult();
    }
    ManageAssetOp const& mManageAsset;

	std::unordered_map<AccountID, CounterpartyDetails> getCounterpartyDetails(Database& db, LedgerDelta* delta) const override;
	SourceDetails getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails) const override;
    

	static bool createAsset(AssetFrame::pointer asset, Application& app, LedgerManager& ledgerManager, Database& db, LedgerDelta& delta);

public:
    
    ManageAssetOpFrame(Operation const& op, OperationResult& res,
                         TransactionFrame& parentTx);

    bool doApply(Application& app, LedgerDelta& delta,
                 LedgerManager& ledgerManager) override;
    bool doCheckValid(Application& app) override;

    static ManageAssetResultCode
    getInnerCode(OperationResult const& res)
    {
        return res.tr().manageAssetResult().code();
    }
};
}
