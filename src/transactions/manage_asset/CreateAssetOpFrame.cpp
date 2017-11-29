// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "CreateAssetOpFrame.h"
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


ReviewableRequestFrame::pointer CreateAssetOpFrame::getUpdatedOrCreateReviewableRequest(Application& app, Database & db, LedgerDelta & delta)
{
	ReviewableRequestFrame::pointer request = getOrCreateReviewableRequest(app, db, delta, ReviewableRequestType::ASSET_CREATE);
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
	auto request = getUpdatedOrCreateReviewableRequest(app, db, delta);
	if (!request) {
		innerResult().code(ManageAssetResultCode::REQUEST_NOT_FOUND);
		return false;
	}

	if (mManageAsset.requestID == 0 && ReviewableRequestFrame::exists(db, getSourceID(), mAssetCreationRequest.code)) {
		innerResult().code(ManageAssetResultCode::ASSET_ALREADY_EXISTS);
		return false;
	}

	auto isAssetExist = AssetFrame::exists(db, mAssetCreationRequest.code);
	if (isAssetExist) {
		innerResult().code(ManageAssetResultCode::ASSET_ALREADY_EXISTS);
		return false;
	}

	if (mManageAsset.requestID == 0) {
		request->storeAdd(delta, db);
	}
	else {
		request->storeChange(delta, db);
	}

	innerResult().code(ManageAssetResultCode::SUCCESS);
	innerResult().success().requestID = request->getRequestID();
	return true;

}

bool CreateAssetOpFrame::doCheckValid(Application & app)
{
	if (!AssetFrame::isAssetCodeValid(mAssetCreationRequest.code)) {
		innerResult().code(ManageAssetResultCode::INVALID_CODE);
		return false;
	}

	if (mAssetCreationRequest.name.empty()) {
		innerResult().code(ManageAssetResultCode::INVALID_NAME);
		return false;
	}

	if (!isValidXDRFlag<AssetPolicy>(mAssetCreationRequest.policies)) {
		innerResult().code(ManageAssetResultCode::INVALID_POLICIES);
		return false;
	}

	return true;
}
}
