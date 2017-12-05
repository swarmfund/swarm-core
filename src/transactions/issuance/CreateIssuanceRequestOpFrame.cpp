// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "util/asio.h"
#include "CreateIssuanceRequestOpFrame.h"
#include "ledger/ReviewableRequestFrame.h"
#include "ledger/ReferenceFrame.h"
#include "util/Logging.h"
#include "util/types.h"
#include "database/Database.h"
#include "ledger/LedgerDelta.h"
#include "main/Application.h"
#include "crypto/SHA.h"
#include "xdrpp/printer.h"

namespace stellar
{

using namespace std;
using xdr::operator==;

CreateIssuanceRequestOpFrame::CreateIssuanceRequestOpFrame(Operation const& op,
                                       OperationResult& res,
                                       TransactionFrame& parentTx)
    : OperationFrame(op, res, parentTx)
    , mCreateIssuanceRequest(mOperation.body.createIssuanceRequestOp())
{
}

bool
CreateIssuanceRequestOpFrame::doApply(Application& app,
                            LedgerDelta& delta, LedgerManager& ledgerManager)
{
	auto request = tryCreateIssuanceRequest(app, delta, ledgerManager);
	if (!request) {
		return false;
	}

        const auto reviewResultCode = approveIssuanceRequest(app, delta, ledgerManager, request);
	bool isFulfilled;
	switch (reviewResultCode) {
	case ReviewRequestResultCode::SUCCESS:
	{
		isFulfilled = true;
		break;
	}
	case ReviewRequestResultCode::INSUFFICIENT_AVAILABLE_FOR_ISSUANCE_AMOUNT:
	{
		isFulfilled = false;
		break;
	}
	case ReviewRequestResultCode::FULL_LINE:
	{
		innerResult().code(CreateIssuanceRequestResultCode::RECEIVER_FULL_LINE);
		return false;
	}
	default: {
		CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected result received on review of just created issuance request: " 
			<< xdr::xdr_traits<ReviewRequestResultCode>::enum_name(reviewResultCode);
		throw std::runtime_error("Unexpected result received on review of just created issuance request");
	}
	}

	innerResult().code(CreateIssuanceRequestResultCode::SUCCESS);
	innerResult().success().requestID = request->getRequestID();
	innerResult().success().fulfilled = isFulfilled;
	auto& db = app.getDatabase();
	auto receiver = BalanceFrame::mustLoadBalance(mCreateIssuanceRequest.request.receiver, db);
	innerResult().success().receiver = receiver->getAccountID();
	return true;
}

bool
CreateIssuanceRequestOpFrame::doCheckValid(Application& app)
{
    
    if (!AssetFrame::isAssetCodeValid(mCreateIssuanceRequest.request.asset)) {
        innerResult().code(CreateIssuanceRequestResultCode::ASSET_NOT_FOUND);
        return false;
    }

	if (mCreateIssuanceRequest.request.amount == 0) {
		innerResult().code(CreateIssuanceRequestResultCode::INVALID_AMOUNT);
		return false;
	}

	if (mCreateIssuanceRequest.reference.empty()) {
		innerResult().code(CreateIssuanceRequestResultCode::REFERENCE_DUPLICATION);
		return false;
	}
	
    return true;
}

std::unordered_map<AccountID, CounterpartyDetails> CreateIssuanceRequestOpFrame::getCounterpartyDetails(Database & db, LedgerDelta * delta) const
{
	// no counterparties
	return{};
}

SourceDetails CreateIssuanceRequestOpFrame::getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails) const
{
	return SourceDetails({AccountType::MASTER, AccountType::SYNDICATE}, mSourceAccount->getHighThreshold(),
                         static_cast<int32_t>(SignerType::ISSUANCE_MANAGER));
}

bool CreateIssuanceRequestOpFrame::isAuthorizedToRequestIssuance(AssetFrame::pointer assetFrame)
{
	return assetFrame->getOwner() == getSourceID();
}

ReviewableRequestFrame::pointer CreateIssuanceRequestOpFrame::tryCreateIssuanceRequest(Application & app, LedgerDelta & delta, LedgerManager & ledgerManager)
{
	Database& db = ledgerManager.getDatabase();
	if (ReviewableRequestFrame::isReferenceExist(db, getSourceID(), mCreateIssuanceRequest.reference)) {
		innerResult().code(CreateIssuanceRequestResultCode::REFERENCE_DUPLICATION);
		return nullptr;
	}

	auto asset = AssetFrame::loadAsset(mCreateIssuanceRequest.request.asset, db);
	if (!asset) {
		innerResult().code(CreateIssuanceRequestResultCode::ASSET_NOT_FOUND);
		return nullptr;
	}

	if (!isAuthorizedToRequestIssuance(asset)) {
		innerResult().code(CreateIssuanceRequestResultCode::NOT_AUTHORIZED);
		return nullptr;
	}

	if (asset->willExceedMaxIssuanceAmount(mCreateIssuanceRequest.request.amount)) {
		innerResult().code(CreateIssuanceRequestResultCode::EXCEEDS_MAX_ISSUANCE_AMOUNT);
		return nullptr;
	}

        auto balance = BalanceFrame::loadBalance(mCreateIssuanceRequest.request.receiver, db);
	if (!balance || balance->getAsset() != asset->getCode()) {
		innerResult().code(CreateIssuanceRequestResultCode::NO_COUNTERPARTY);
		return nullptr;
	}

	auto reference = xdr::pointer<stellar::string64>(new stellar::string64(mCreateIssuanceRequest.reference));
	ReviewableRequestEntry::_body_t body;
	body.type(ReviewableRequestType::ISSUANCE_CREATE);
	body.issuanceRequest() = mCreateIssuanceRequest.request;
	auto request = ReviewableRequestFrame::createNewWithHash(delta.getHeaderFrame().generateID(), getSourceID(), asset->getOwner(), reference, body);
	request->storeAdd(delta, db);
	return request;
}

std::pair<bool, ReviewRequestResult>  CreateIssuanceRequestOpFrame::tryReviewIssuanceRequest(Application & app, LedgerDelta & delta,
	LedgerManager & ledgerManager, ReviewableRequestFrame::pointer request)
{
	Operation op;
	auto reviewer = request->getReviewer();
	op.sourceAccount = xdr::pointer<stellar::AccountID>(new AccountID(reviewer));
	op.body.type(OperationType::REVIEW_REQUEST);
	ReviewRequestOp& reviewRequestOp = op.body.reviewRequestOp();
	reviewRequestOp.action = ReviewRequestOpAction::APPROVE;
	reviewRequestOp.requestHash = request->getHash();
	reviewRequestOp.requestID = request->getRequestID();
	reviewRequestOp.requestType = request->getRequestType();

	OperationResult opRes;
	opRes.code(OperationResultCode::opINNER);
	opRes.tr().type(OperationType::REVIEW_REQUEST);
	Database& db = ledgerManager.getDatabase();
	auto reviewerFrame = AccountFrame::loadAccount(reviewer, db);
	if (!reviewerFrame) {
		CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected state: expected review to exist for request: " << xdr::xdr_to_string(request->getRequestEntry());
		throw std::runtime_error("Unexpected state expected reviewer to exist");
	}

	auto reviewRequestOpFrame = std::make_shared<ReviewIssuanceCreationRequestOpFrame>(op, opRes, mParentTx);
	reviewRequestOpFrame->setSourceAccountPtr(reviewerFrame);
	bool isApplied = reviewRequestOpFrame->doCheckValid(app) && reviewRequestOpFrame->doApply(app, delta, ledgerManager);
	if (reviewRequestOpFrame->getResultCode() != OperationResultCode::opINNER)
	{
		throw std::runtime_error("Unexpected error code from review issuance creation request");
	}

	return std::pair<bool, ReviewRequestResult>(isApplied, reviewRequestOpFrame->getResult().tr().reviewRequestResult());
}

ReviewRequestResultCode CreateIssuanceRequestOpFrame::approveIssuanceRequest(Application & app, LedgerDelta & delta, LedgerManager & ledgerManager,
	ReviewableRequestFrame::pointer request)
{
	Database& db = ledgerManager.getDatabase();
	// shield outer scope of any side effects by using
    // a sql transaction for ledger state and LedgerDelta
	soci::transaction reviewRequestTx(db.getSession());
	LedgerDelta reviewRequestDelta(delta);

	auto result = tryReviewIssuanceRequest(app, reviewRequestDelta, ledgerManager, request);
	bool isApplied = result.first;
	ReviewRequestResult reviewRequestResult = result.second;
	if (!isApplied)
	{
		return reviewRequestResult.code();
	}

	auto resultCode = reviewRequestResult.code();
	if (resultCode != ReviewRequestResultCode::SUCCESS) {
		CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected state: doApply returned true, but result code is not success: " << xdr::xdr_to_string(request->getRequestEntry());
		throw std::runtime_error("Unexpected state: doApply returned true, but result code is not success:  for review create issuance request");
	}

	reviewRequestTx.commit();
	reviewRequestDelta.commit();
	
	return resultCode;
}

}
