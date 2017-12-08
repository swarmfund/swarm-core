// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "util/asio.h"
#include "ReviewRequestOpFrame.h"
#include "ReviewAssetCreationRequestOpFrame.h"
#include "ReviewAssetUpdateRequestOpFrame.h"
#include "ReviewIssuanceCreationRequestOpFrame.h"
#include "ReviewPreIssuanceCreationRequestOpFrame.h"
#include "util/Logging.h"
#include "util/types.h"
#include "database/Database.h"
#include "ledger/LedgerDelta.h"
#include "ledger/ReviewableRequestFrame.h"
#include "ledger/ReviewableRequestHelper.h"
#include "ledger/ReferenceFrame.h"
#include "ledger/ReferenceHelper.h"
#include "main/Application.h"

namespace stellar
{

using namespace std;
using xdr::operator==;

void ReviewRequestOpFrame::createReference(LedgerDelta & delta, Database & db, AccountID const & requestor, xdr::pointer<stellar::string64> reference)
{
	if (!reference) {
		CLOG(ERROR, Logging::OPERATION_LOGGER) << "Expected reference not to be nullptr: requestID " << mReviewRequest.requestID;
		throw std::invalid_argument("Expected reference not to be nullptr");
	}

	auto referenceHelper = ReferenceHelper::Instance();
	auto isReferenceAlreadyExists = referenceHelper->exists(db, *reference, requestor);
	if (isReferenceAlreadyExists) {
		CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected state: reference already exists. requestID " << mReviewRequest.requestID;
		throw std::runtime_error("Reference already exists");
	}

	auto referenceFrame = ReferenceFrame::create(requestor, *reference);
	EntryHelperProvider::storeAddEntry(delta, db, referenceFrame->mEntry);
}

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
	case ReviewableRequestType::ISSUANCE_CREATE:
		return new ReviewIssuanceCreationRequestOpFrame(op, res, parentTx);
	case ReviewableRequestType::PRE_ISSUANCE_CREATE:
		return new ReviewPreIssuanceCreationRequestOpFrame(op, res, parentTx);
	default:
		throw std::runtime_error("Unexpceted request type for review request op");
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
	EntryHelperProvider::storeChangeEntry(delta, db, request->mEntry);
	innerResult().code(ReviewRequestResultCode::SUCCESS);
	return true;
}

bool ReviewRequestOpFrame::handlePermanentReject(Application & app, LedgerDelta & delta, LedgerManager & ledgerManager, ReviewableRequestFrame::pointer request)
{
	Database& db = ledgerManager.getDatabase();
	EntryHelperProvider::storeDeleteEntry(delta, db, request->getKey());
	innerResult().code(ReviewRequestResultCode::SUCCESS);
	return true;
}

bool
ReviewRequestOpFrame::doApply(Application& app,
                            LedgerDelta& delta, LedgerManager& ledgerManager)
{

	Database& db = ledgerManager.getDatabase();
	auto reviewableRequestHelper = ReviewableRequestHelper::Instance();
	auto request = reviewableRequestHelper->loadRequest(mReviewRequest.requestID, db, &delta);
	if (!request) {
		innerResult().code(ReviewRequestResultCode::NOT_FOUND);
		return false;
	}

	if (request->getHash() != mReviewRequest.requestHash) {
		innerResult().code(ReviewRequestResultCode::HASH_MISMATCHED);
		return false;
	}

	if (request->getRequestType() != mReviewRequest.requestType) {
		innerResult().code(ReviewRequestResultCode::TYPE_MISMATCHED);
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
		innerResult().code(ReviewRequestResultCode::INVALID_ACTION);
		return false;
	}

	if (!isRejectReasonValid()) {
		innerResult().code(ReviewRequestResultCode::INVALID_REASON);
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
