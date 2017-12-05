// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "UpdateAssetOpFrame.h"
#include "ledger/LedgerDelta.h"

#include "database/Database.h"

#include "main/Application.h"

namespace stellar
{

using namespace std;
using xdr::operator==;
    

ReviewableRequestFrame::pointer UpdateAssetOpFrame::getUpdatedOrCreateReviewableRequest(Application& app, Database & db, LedgerDelta & delta)
{
	ReviewableRequestFrame::pointer request = getOrCreateReviewableRequest(app, db, delta, ReviewableRequestType::ASSET_UPDATE);
	if (!request)
		return nullptr;

	ReviewableRequestEntry& requestEntry = request->getRequestEntry();
	requestEntry.body.type(ReviewableRequestType::ASSET_UPDATE);
	requestEntry.body.assetUpdateRequest() = mAssetUpdateRequest;
	request->recalculateHashRejectReason();
	return request;
}

UpdateAssetOpFrame::UpdateAssetOpFrame(Operation const & op, OperationResult & res, TransactionFrame & parentTx) :
	ManageAssetOpFrame(op, res, parentTx), mAssetUpdateRequest(mManageAsset.request.updateAsset())
{
}

bool UpdateAssetOpFrame::doApply(Application & app, LedgerDelta & delta, LedgerManager & ledgerManager)
{
	Database& db = ledgerManager.getDatabase();
	auto request = getUpdatedOrCreateReviewableRequest(app, db, delta);
	if (!request) {
		innerResult().code(ManageAssetResultCode::REQUEST_NOT_FOUND);
		return false;
	}

	auto assetFrame = AssetFrame::loadAsset(mAssetUpdateRequest.code, getSourceID(), db, &delta);
	if (!assetFrame) {
		innerResult().code(ManageAssetResultCode::ASSET_NOT_FOUND);
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

bool UpdateAssetOpFrame::doCheckValid(Application & app)
{
	if (!AssetFrame::isAssetCodeValid(mAssetUpdateRequest.code)) {
		innerResult().code(ManageAssetResultCode::INVALID_CODE);
		return false;
	}

	if (!isValidXDRFlag<AssetPolicy>(mAssetUpdateRequest.policies)) {
		innerResult().code(ManageAssetResultCode::INVALID_POLICIES);
		return false;
	}

	return true;
}

string UpdateAssetOpFrame::getAssetCode() const
{
    return mAssetUpdateRequest.code;
}
}
