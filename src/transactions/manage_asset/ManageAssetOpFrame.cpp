// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ManageAssetOpFrame.h"
#include "CancelAssetRequestOpFrame.h"
#include "CreateAssetOpFrame.h"
#include "UpdateAssetOpFrame.h"
#include "ledger/LedgerDelta.h"
#include "ledger/ReviewableRequestHelper.h"
#include "main/Application.h"

namespace stellar
{

using namespace std;
using xdr::operator==;
    
ManageAssetOpFrame::ManageAssetOpFrame(Operation const& op,
                                           OperationResult& res,
                                           TransactionFrame& parentTx)
    : OperationFrame(op, res, parentTx)
    , mManageAsset(mOperation.body.manageAssetOp())
{
}

std::unordered_map<AccountID, CounterpartyDetails> ManageAssetOpFrame::getCounterpartyDetails(Database & db, LedgerDelta * delta) const
{
	// no counterparties
	return std::unordered_map<AccountID, CounterpartyDetails>();
}

SourceDetails ManageAssetOpFrame::getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails) const
{
	return SourceDetails({AccountType::MASTER, AccountType::SYNDICATE}, mSourceAccount->getHighThreshold(),
						 static_cast<int32_t>(SignerType::ASSET_MANAGER));
}

ManageAssetOpFrame* ManageAssetOpFrame::makeHelper(Operation const & op, OperationResult & res, TransactionFrame & parentTx)
{
	switch (op.body.manageAssetOp().request.action()) {
	case ManageAssetAction::CREATE_ASSET_CREATION_REQUEST:
		return new CreateAssetOpFrame(op, res, parentTx);
	case ManageAssetAction::CREATE_ASSET_UPDATE_REQUEST:
		return new UpdateAssetOpFrame(op, res, parentTx);
	case ManageAssetAction::CANCEL_ASSET_REQUEST:
		return new CancelAssetRequestOpFrame(op, res, parentTx);
	default:
		throw std::runtime_error("Unexpected action in manage asset op");
	}
}	

ReviewableRequestFrame::pointer ManageAssetOpFrame::getOrCreateReviewableRequest(Application& app, Database& db, LedgerDelta& delta, ReviewableRequestType requestType)
{
	if (mManageAsset.requestID == 0) {
		return ReviewableRequestFrame::createNew(delta.getHeaderFrame().generateID(), getSourceID(), app.getMasterID(), nullptr);
	}

	auto reviewableRequestHelper = ReviewableRequestHelper::Instance();
	return reviewableRequestHelper->loadRequest(mManageAsset.requestID, getSourceID(), requestType, db, &delta);
}
}
