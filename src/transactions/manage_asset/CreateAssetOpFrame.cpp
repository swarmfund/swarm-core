// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include <lib/xdrpp/xdrpp/printer.h>
#include <transactions/review_request/ReviewIssuanceCreationRequestOpFrame.h>
#include <main/Application.h>
#include <transactions/review_request/ReviewRequestHelper.h>
#include "CreateAssetOpFrame.h"
#include "ledger/AccountHelper.h"
#include "ledger/AssetHelperLegacy.h"
#include "ledger/ReviewableRequestHelper.h"

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

SourceDetails CreateAssetOpFrame::getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
                                                          int32_t ledgerVersion) const
{
    vector<AccountType> allowedAccountTypes = {AccountType::MASTER};

    bool isBaseAsset = isSetFlag(mAssetCreationRequest.policies, AssetPolicy::BASE_ASSET);
    bool isStatsAsset = isSetFlag(mAssetCreationRequest.policies, AssetPolicy::STATS_QUOTE_ASSET);
    if (!isBaseAsset && !isStatsAsset)
    {
        allowedAccountTypes.push_back(AccountType::SYNDICATE);
    }

    return SourceDetails(allowedAccountTypes, mSourceAccount->getHighThreshold(),
                             static_cast<int32_t>(SignerType::ASSET_MANAGER));
}

ReviewableRequestFrame::pointer CreateAssetOpFrame::getUpdatedOrCreateReviewableRequest(Application& app, Database & db, LedgerDelta & delta) const
{
    ReviewableRequestFrame::pointer request = getOrCreateReviewableRequest(app, db, delta, ReviewableRequestType::ASSET_CREATE);
	if (!request) {
        return nullptr;
    }

    ReviewableRequestEntry& requestEntry = request->getRequestEntry();
	requestEntry.body.type(ReviewableRequestType::ASSET_CREATE);
	requestEntry.body.assetCreationRequest() = mAssetCreationRequest;
	request->recalculateHashRejectReason();

	return request;
}

bool CreateAssetOpFrame::doApply(Application & app, LedgerDelta & delta, LedgerManager & ledgerManager)
{
	Database& db = ledgerManager.getDatabase();

	auto reviewableRequestHelper = ReviewableRequestHelper::Instance();
    if (mManageAsset.requestID == 0 && reviewableRequestHelper->exists(db, getSourceID(), mAssetCreationRequest.code)) {
        innerResult().code(ManageAssetResultCode::REQUEST_ALREADY_EXISTS);
        return false;
    }

	auto assetHelper = AssetHelperLegacy::Instance();

    auto isAssetExist = assetHelper->exists(db, mAssetCreationRequest.code);
    if (isAssetExist) {
        innerResult().code(ManageAssetResultCode::ASSET_ALREADY_EXISTS);
        return false;
    }

    bool isStats = isSetFlag(mAssetCreationRequest.policies, AssetPolicy::STATS_QUOTE_ASSET);
    if (isStats && !!assetHelper->loadStatsAsset(db)) {
        innerResult().code(ManageAssetResultCode::STATS_ASSET_ALREADY_EXISTS);
        return false;
    }

	auto request = getUpdatedOrCreateReviewableRequest(app, db, delta);
	if (!request) {
        innerResult().code(ManageAssetResultCode::REQUEST_NOT_FOUND);
		return false;
	}

    if (mManageAsset.requestID == 0) {
        EntryHelperProvider::storeAddEntry(delta, db, request->mEntry);
    }
    else {
		EntryHelperProvider::storeChangeEntry(delta, db, request->mEntry);
    }

    bool fulfilled = false;
    if (getSourceAccount().getAccountType() == AccountType::MASTER) {
        auto resultCode = ReviewRequestHelper::tryApproveRequest(mParentTx, app, ledgerManager, delta, request);

        if (resultCode != ReviewRequestResultCode::SUCCESS) {
            throw std::runtime_error("Failed to review create asset request");
        }
        fulfilled = true;
    }

    innerResult().code(ManageAssetResultCode::SUCCESS);
	innerResult().success().requestID = request->getRequestID();
    innerResult().success().fulfilled = fulfilled;
	return true;
}

bool CreateAssetOpFrame::doCheckValid(Application & app)
{
	if (!AssetFrame::isAssetCodeValid(mAssetCreationRequest.code)) {
		innerResult().code(ManageAssetResultCode::INVALID_CODE);
		return false;
	}

	if (!isValidXDRFlag<AssetPolicy>(mAssetCreationRequest.policies)) {
		innerResult().code(ManageAssetResultCode::INVALID_POLICIES);
		return false;
	}

    if (mAssetCreationRequest.maxIssuanceAmount < mAssetCreationRequest.initialPreissuedAmount)
    {
        innerResult().code(ManageAssetResultCode::INITIAL_PREISSUED_EXCEEDS_MAX_ISSUANCE);
        return false;
    }

    if (!isValidJson(mAssetCreationRequest.details))
    {
        innerResult().code(ManageAssetResultCode::INVALID_DETAILS);
        return false;
    }

	return true;
}

string CreateAssetOpFrame::getAssetCode() const
{
    return mAssetCreationRequest.code;
}

}
