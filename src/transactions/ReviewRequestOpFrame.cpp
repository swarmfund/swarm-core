// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "util/asio.h"
#include "transactions/ReviewRequestOpFrame.h"
#include "transactions/ReviewAssetCreationRequestOpFrame.h"
#include "transactions/ReviewAssetUpdateRequestOpFrame.h"
#include "util/Logging.h"
#include "util/types.h"
#include "database/Database.h"
#include "ledger/LedgerDelta.h"
#include "ledger/ReviewableRequestFrame.h"
#include "main/Application.h"

namespace stellar
{

using namespace std;
using xdr::operator==;

ReviewRequestOpFrame::ReviewRequestOpFrame(Operation const& op,
                                       OperationResult& res,
                                       TransactionFrame& parentTx)
    : OperationFrame(op, res, parentTx)
    , mReviewRequest(mOperation.body.reviewRequestOp())
{
}

ReviewRequestOpFrame* ReviewRequestOpFrame::makeHelper(Operation const & op, OperationResult & res, TransactionFrame & parentTx)
{
	switch (op.body.reviewRequestOp().requestType) {
	case ReviewableRequestType::ASSET_CREATE:
		return new ReviewAssetCreationRequestOpFrame(op, res, parentTx);
	case ReviewableRequestType::ASSET_UPDATE:
		return new ReviewAssetUpdateRequestOpFrame(op, res, parentTx);
	default:
		return new ReviewRequestOpFrame(op, res, parentTx);
	}
}

std::unordered_map<AccountID, CounterpartyDetails> ReviewRequestOpFrame::getCounterpartyDetails(Database & db, LedgerDelta * delta) const
{
	// no counterparties
	return std::unordered_map<AccountID, CounterpartyDetails>();
}

bool ReviewRequestOpFrame::handleReject(Application & app, LedgerDelta & delta, LedgerManager & ledgerManager, ReviewableRequestFrame::pointer request)
{
	request->setRejectReason(mReviewRequest.reason);
	Database& db = ledgerManager.getDatabase();
	request->storeChange(delta, db);
	innerResult().code(REVIEW_REQUEST_SUCCESS);
	return true;
}

bool ReviewRequestOpFrame::handlePermanentReject(Application & app, LedgerDelta & delta, LedgerManager & ledgerManager, ReviewableRequestFrame::pointer request)
{
	Database& db = ledgerManager.getDatabase();
	request->storeDelete(delta, db);
	innerResult().code(REVIEW_REQUEST_SUCCESS);
	return true;
}

bool
ReviewRequestOpFrame::doApply(Application& app,
                            LedgerDelta& delta, LedgerManager& ledgerManager)
{

	Database& db = ledgerManager.getDatabase();
	auto request = ReviewableRequestFrame::loadRequest(mReviewRequest.requestID, db, &delta);
	if (!request) {
		innerResult().code(REVIEW_REQUEST_NOT_FOUND);
		return false;
	}

	if (request->getHash() != mReviewRequest.requestHash) {
		innerResult().code(REVIEW_REQUEST_HASH_MISMATCHED);
		return false;
	}

	if (request->getRequestType() != mReviewRequest.requestType) {
		innerResult().code(REVIEW_REQUEST_TYPE_MISMATCHED);
		return false;
	}

	switch (mReviewRequest.action) {
	case ReviewRequestOpAction::APPROVE:
		return handleApprove(app, delta, ledgerManager, request);
	case ReviewRequestOpAction::REJECT:
		return handleReject(app, delta, ledgerManager, request);
	case ReviewRequestOpAction::PERMANENT_REJECT:
		return handlePermanentReject(app, delta, ledgerManager, request);
	default:
		CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected action for review request op: " << xdr::xdr_traits<ReviewRequestOpAction>::enum_name(mReviewRequest.action);
		throw std::runtime_error("Unexpected action for review request op");
	}
}

bool
ReviewRequestOpFrame::doCheckValid(Application& app)
{
	if (!isValidEnumValue(mReviewRequest.action)) {
		innerResult().code(REVIEW_REQUEST_INVALID_ACTION);
		return false;
	}

	if (!isRejectReasonValid()) {
		innerResult().code(REVIEW_REQUEST_INVALID_REASON);
		return false;
	}
    

    return true;
}

bool ReviewRequestOpFrame::isRejectReasonValid()
{
	if (mReviewRequest.action == ReviewRequestOpAction::APPROVE) {
		return mReviewRequest.reason.empty();
	}

	return !mReviewRequest.reason.empty();
}

}
