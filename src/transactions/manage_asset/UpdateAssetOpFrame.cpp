// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include <transactions/review_request/ReviewRequestHelper.h>
#include "UpdateAssetOpFrame.h"
#include "ledger/LedgerDelta.h"

#include "database/Database.h"

#include "main/Application.h"

namespace stellar
{

using namespace std;
using xdr::operator==;

SourceDetails UpdateAssetOpFrame::getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails) const
{
    vector<AccountType> allowedAccountTypes = {AccountType::MASTER};

    bool isBaseAsset = checkAssetPolicy(AssetPolicy::BASE_ASSET);
    bool isStatsAsset = checkAssetPolicy(AssetPolicy::STATS_QUOTE_ASSET);
    if (!isBaseAsset && !isStatsAsset)
    {
        allowedAccountTypes.push_back(AccountType::SYNDICATE);
    }

    return SourceDetails(allowedAccountTypes, mSourceAccount->getHighThreshold(),
                         static_cast<int32_t>(SignerType::ASSET_MANAGER));
}

ReviewableRequestFrame::pointer UpdateAssetOpFrame::getUpdatedOrCreateReviewableRequest(Application& app, Database & db, LedgerDelta & delta)
{
    ReviewableRequestFrame::pointer request = getOrCreateReviewableRequest(app, db, delta, ReviewableRequestType::ASSET_UPDATE);
    if (!request) {
        return nullptr;
    }

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
    auto assetFrame = AssetFrame::loadAsset(mAssetUpdateRequest.code, getSourceID(), db, &delta);
    if (!assetFrame) {
        innerResult().code(ManageAssetResultCode::ASSET_NOT_FOUND);
        return false;
    }

    bool isStats = checkAssetPolicy(AssetPolicy::STATS_QUOTE_ASSET);
    if (isStats) {
        auto statsAssetFrame = AssetFrame::loadStatsAsset(db);
        if (statsAssetFrame && mAssetUpdateRequest.code != statsAssetFrame->getCode()) {
            innerResult().code(ManageAssetResultCode::ASSET_ALREADY_EXISTS);
            return false;
        }
    }

    auto requestor = AccountFrame::loadAccount(getSourceID(), db);
    if (!requestor)
        throw std::runtime_error("Unexpected state. Source account supposed to exist");
    bool isMaster = requestor->getAccountType() == AccountType::MASTER;
    bool updBaseOrStats = assetFrame->checkPolicy(AssetPolicy::BASE_ASSET) ||
                          assetFrame->checkPolicy(AssetPolicy::STATS_QUOTE_ASSET);
    if (!isMaster && updBaseOrStats) {
        innerResult().code(ManageAssetResultCode::NO_PERMISSIONS);
        return false;
    }

	auto request = getUpdatedOrCreateReviewableRequest(app, db, delta);
	if (!request) {
        innerResult().code(ManageAssetResultCode::ASSET_NOT_FOUND);
		return false;
	}

	if (mManageAsset.requestID == 0) {
		request->storeAdd(delta, db);
	}
	else {
		request->storeChange(delta, db);
	}

    bool fulfilled = false;
    if (isMaster) {
        ReviewRequestHelper reviewRequestHelper(app, ledgerManager, delta, request);
        ReviewRequestResultCode resultCode = reviewRequestHelper.tryApproveRequest(mParentTx);

        if (resultCode != ReviewRequestResultCode::SUCCESS) {
            throw std::runtime_error("Failed to approve review request");
        }
        fulfilled = true;
    }

	innerResult().code(ManageAssetResultCode::SUCCESS);
	innerResult().success().requestID = request->getRequestID();
    innerResult().success().fulfilled = fulfilled;
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

bool UpdateAssetOpFrame::checkAssetPolicy(AssetPolicy policy) const
{
    uint32 assetPolicy = static_cast<uint32>(policy);
    return (mAssetUpdateRequest.policies && assetPolicy) == assetPolicy;
}
}
