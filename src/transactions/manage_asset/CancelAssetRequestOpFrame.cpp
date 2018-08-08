// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "CancelAssetRequestOpFrame.h"

#include "ledger/LedgerDelta.h"
#include "ledger/EntryHelperLegacy.h"
#include "ledger/ReviewableRequestFrame.h"
#include "ledger/ReviewableRequestHelper.h"

#include "database/Database.h"

#include "main/Application.h"
#include "medida/meter.h"
#include "medida/metrics_registry.h"

namespace stellar
{

using namespace std;
using xdr::operator==;
    
CancelAssetRequestOpFrame::CancelAssetRequestOpFrame(Operation const& op,
                                           OperationResult& res,
                                           TransactionFrame& parentTx)
    : ManageAssetOpFrame(op, res, parentTx)
{
}

bool
CancelAssetRequestOpFrame::doApply(Application& app,
                              LedgerDelta& delta, LedgerManager& ledgerManager)
{
    Database& db = ledgerManager.getDatabase();
	
	auto reviewableRequestHelper = ReviewableRequestHelper::Instance();
	auto request = reviewableRequestHelper->loadRequest(mManageAsset.requestID, getSourceID(), db, &delta);
	if (!request) {
		innerResult().code(ManageAssetResultCode::REQUEST_NOT_FOUND);
		return false;
	}

	auto requestType = request->getRequestEntry().body.type();
	if (requestType != ReviewableRequestType::ASSET_CREATE && requestType != ReviewableRequestType::ASSET_UPDATE) {
		innerResult().code(ManageAssetResultCode::REQUEST_NOT_FOUND);
		return false;
	}

 	EntryHelperProvider::storeDeleteEntry(delta, db, request->getKey());

	innerResult().code(ManageAssetResultCode::SUCCESS);
	innerResult().success().requestID = request->getRequestID();
	return true;
}

bool CancelAssetRequestOpFrame::doCheckValid(Application& app)
{
	if (mManageAsset.requestID == 0)
	{
		innerResult().code(ManageAssetResultCode::REQUEST_NOT_FOUND);
		return false;
	}

    return true;
}

string CancelAssetRequestOpFrame::getAssetCode() const
{
    throw runtime_error("Unexpected method call. No asset code is required for asset request cancelation.");
}
}
