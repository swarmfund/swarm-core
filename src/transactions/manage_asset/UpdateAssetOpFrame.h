#pragma once

// Copyright 2015 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ManageAssetOpFrame.h"
#include "ledger/ReviewableRequestFrame.h"

namespace stellar
{

class UpdateAssetOpFrame : public ManageAssetOpFrame
{
	AssetUpdateRequest const& mAssetUpdateRequest;

	// Returns update already existing request from db or creates new one.
	// if fails to load request, returns nullptr
	ReviewableRequestFrame::pointer getUpdatedOrCreateReviewableRequest(Application& app, Database& db, LedgerDelta& delta);

public:
    
	UpdateAssetOpFrame(Operation const& op, OperationResult& res,
                         TransactionFrame& parentTx);

    bool doApply(Application& app, LedgerDelta& delta,
                 LedgerManager& ledgerManager) override;
    bool doCheckValid(Application& app) override;
};
}
