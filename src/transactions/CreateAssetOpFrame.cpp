// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "transactions/CreateAssetOpFrame.h"
#include "database/Database.h"
#include "ledger/LedgerDelta.h"
#include "ledger/AssetFrame.h"

namespace stellar
{

using namespace std;
using xdr::operator==;

CreateAssetOpFrame::CreateAssetOpFrame(Operation const& op,
                                           OperationResult& res,
                                           TransactionFrame& parentTx)
    : ManageAssetOpFrame(op, res, parentTx), mAssetCreationRequest(mManageAsset.request.createAsset())
{
	
}


ReviewableRequestFrame::pointer CreateAssetOpFrame::getUpdatedOrCreateReviewableRequest(Database & db, LedgerDelta & delta)
{
	ReviewableRequestFrame::pointer request = getOrCreateReviewableRequest(db, delta, ReviewableRequestType::ASSET_CREATE);
	if (!request)
		return nullptr;

	ReviewableRequestEntry& requestEntry = request->getRequestEntry();
	requestEntry.body.type(ReviewableRequestType::ASSET_CREATE);
	requestEntry.body.assetCreationRequest() = mAssetCreationRequest;
	request->recalculateHashRejectReason();
	return request;
}

bool CreateAssetOpFrame::doApply(Application & app, LedgerDelta & delta, LedgerManager & ledgerManager)
{
	Database& db = ledgerManager.getDatabase();
	auto request = getUpdatedOrCreateReviewableRequest(db, delta);
	if (!request) {
		innerResult().code(MANAGE_ASSET_REQUEST_NOT_FOUND);
		return false;
	}

	auto isAssetExist = AssetFrame::exists(db, mAssetCreationRequest.code);
	if (isAssetExist) {
		innerResult().code(MANAGE_ASSET_ASSET_ALREADY_EXISTS);
		return false;
	}

	if (mManageAsset.requestID == 0) {
		request->storeAdd(delta, db);
	}
	else {
		request->storeChange(delta, db);
	}

	innerResult().code(MANAGE_ASSET_SUCCESS);
	innerResult().success().requestID = request->getRequestID();
	return true;

}

bool CreateAssetOpFrame::doCheckValid(Application & app)
{
	if (!isAssetValid(mAssetCreationRequest.code)) {
		innerResult().code(MANAGE_ASSET_INVALID_CODE);
		return false;
	}

	if (mAssetCreationRequest.name.empty()) {
		innerResult().code(MANAGE_ASSET_INVALID_NAME);
		return false;
	}

	if (!isValidXDRFlag<AssetPolicy>(mAssetCreationRequest.policies)) {
		innerResult().code(MANAGE_ASSET_INVALID_POLICIES);
		return false;
	}

	return true;
}
}
