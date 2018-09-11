// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ManageAssetOpFrame.h"
#include "CancelAssetRequestOpFrame.h"
#include "CreateAssetOpFrame.h"
#include "UpdateAssetOpFrame.h"
#include "ChangeAssetMaxIssuanceOpFrame.h"
#include "ledger/LedgerDelta.h"
#include "ledger/ReviewableRequestHelper.h"
#include "main/Application.h"
#include "ChangeAssetPreIssuerOpFrame.h"

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

SourceDetails ManageAssetOpFrame::getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
                                                          int32_t ledgerVersion) const
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
        case ManageAssetAction::CHANGE_PREISSUED_ASSET_SIGNER:
            return new ChangeAssetPreIssuerOpFrame(op, res, parentTx);
        case ManageAssetAction::UPDATE_MAX_ISSUANCE:
            return new ChangeAssetMaxIssuanceOpFrame(op, res, parentTx);
	default:
		throw runtime_error("Unexpected action in manage asset op");
	}
}	

ReviewableRequestFrame::pointer ManageAssetOpFrame::getOrCreateReviewableRequest(Application& app, Database& db, LedgerDelta& delta, const ReviewableRequestType requestType) const
{
	if (mManageAsset.requestID == 0) {
	        const auto reference = xdr::pointer<string64>(new string64(getAssetCode()));
		return ReviewableRequestFrame::createNew(delta, getSourceID(), app.getMasterID(), reference,
                                                 app.getLedgerManager().getCloseTime());
	}

	auto reviewableRequestHelper = ReviewableRequestHelper::Instance();
	return reviewableRequestHelper->loadRequest(mManageAsset.requestID, getSourceID(), requestType, db, &delta);
}
}
