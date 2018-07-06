// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include <ledger/AccountHelper.h>
#include "util/asio.h"
#include "ReviewRequestOpFrame.h"
#include "ReviewAssetCreationRequestOpFrame.h"
#include "ReviewAssetUpdateRequestOpFrame.h"
#include "ReviewIssuanceCreationRequestOpFrame.h"
#include "ReviewLimitsUpdateRequestOpFrame.h"
#include "ReviewPreIssuanceCreationRequestOpFrame.h"
#include "ReviewWithdrawalRequestOpFrame.h"
#include "ReviewUpdateSaleDetailsRequestOpFrame.h"
#include "util/Logging.h"
#include "util/types.h"
#include "database/Database.h"
#include "ledger/LedgerDelta.h"
#include "ledger/ReviewableRequestFrame.h"
#include "ledger/ReviewableRequestHelper.h"
#include "ledger/ReferenceFrame.h"
#include "ledger/ReferenceHelper.h"
#include "main/Application.h"
#include "ReviewSaleCreationRequestOpFrame.h"
#include "ReviewAMLAlertRequestOpFrame.h"
#include "ReviewUpdateKYCRequestOpFrame.h"
#include "ReviewUpdateSaleEndTimeRequestOpFrame.h"

namespace stellar
{

using namespace std;
using xdr::operator==;

bool ReviewRequestOpFrame::areBlockingRulesFulfilled(ReviewableRequestFrame::pointer request, LedgerManager& lm, Database & db, LedgerDelta & delta)
{
    auto requestorAccount = AccountHelper::Instance()->loadAccount(request->getRequestor(), db, &delta);
    // just go through old flow
    if (!lm.shouldUse(LedgerVersion::ALLOW_REJECT_REQUEST_OF_BLOCKED_REQUESTOR)) {
        if (isSetFlag(requestorAccount->getBlockReasons(), BlockReasons::SUSPICIOUS_BEHAVIOR)) {
            innerResult().code(ReviewRequestResultCode::REQUESTOR_IS_BLOCKED);
            return false;
        }

        return true;
    }

    // we do not care about user state if it's not approval
    if (mReviewRequest.action != ReviewRequestOpAction::APPROVE) {
        return true;
    }

    if (isSetFlag(requestorAccount->getBlockReasons(), BlockReasons::SUSPICIOUS_BEHAVIOR)) {
        innerResult().code(ReviewRequestResultCode::REQUESTOR_IS_BLOCKED);
        return false;
    }

    return true;
    
}

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
	switch (op.body.reviewRequestOp().requestDetails.requestType()) {
	case ReviewableRequestType::ASSET_CREATE:
		return new ReviewAssetCreationRequestOpFrame(op, res, parentTx);
	case ReviewableRequestType::ASSET_UPDATE:
		return new ReviewAssetUpdateRequestOpFrame(op, res, parentTx);
	case ReviewableRequestType::ISSUANCE_CREATE:
		return new ReviewIssuanceCreationRequestOpFrame(op, res, parentTx);
	case ReviewableRequestType::PRE_ISSUANCE_CREATE:
		return new ReviewPreIssuanceCreationRequestOpFrame(op, res, parentTx);
	case ReviewableRequestType::WITHDRAW:
		return new ReviewWithdrawalRequestOpFrame(op, res, parentTx);
	case ReviewableRequestType::SALE:
		return new ReviewSaleCreationRequestOpFrame(op, res, parentTx);
	case ReviewableRequestType::LIMITS_UPDATE:
		return new ReviewLimitsUpdateRequestOpFrame(op, res, parentTx);
    case ReviewableRequestType::TWO_STEP_WITHDRAWAL:
        return new ReviewTwoStepWithdrawalRequestOpFrame(op, res, parentTx);
	case ReviewableRequestType::AML_ALERT:
		return new ReviewAMLAlertRequestOpFrame(op,res,parentTx);
    case ReviewableRequestType::UPDATE_KYC:
        return new ReviewUpdateKYCRequestOpFrame(op, res, parentTx);
	case ReviewableRequestType::UPDATE_SALE_DETAILS:
		return new ReviewUpdateSaleDetailsRequestOpFrame(op, res, parentTx);
	case ReviewableRequestType::UPDATE_SALE_END_TIME:
		return new ReviewUpdateSaleEndTimeRequestOpFrame(op, res, parentTx);
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
	auto request = ReviewableRequestHelper::Instance()->loadRequest(mReviewRequest.requestID, db, &delta);
	if (!request || !(request->getReviewer() == getSourceID())) {
		innerResult().code(ReviewRequestResultCode::NOT_FOUND);
		return false;
	}

        if (!areBlockingRulesFulfilled(request, ledgerManager, db, delta)) {
            return false;
        }

	if (!(request->getHash() == mReviewRequest.requestHash)) {
		innerResult().code(ReviewRequestResultCode::HASH_MISMATCHED);
		return false;
	}

	if (request->getRequestType() != mReviewRequest.requestDetails.requestType()) {
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

	if (!isRejectReasonValid(app)) {
		innerResult().code(ReviewRequestResultCode::INVALID_REASON);
		return false;
	}
    

    return true;
}

bool ReviewRequestOpFrame::isRejectReasonValid(Application& app)
{
	if (mReviewRequest.action == ReviewRequestOpAction::APPROVE) {
		return mReviewRequest.reason.empty();
	}

	if (mReviewRequest.reason.empty()) {
		return false;
	}

	return mReviewRequest.reason.length() <= app.getRejectReasonMaxLength();
}

uint64_t ReviewRequestOpFrame::getTotalFee(uint64_t requestID, Fee fee)
{
    uint64_t totalFee = 0;
    if (!safeSum(requestID, fee.fixed, fee.percent)) {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Failed to calculate total fee for issuance request: " << requestID;
        throw runtime_error("Failed to calculate total fee for issuance request");
    }

    return totalFee;
}    

}
