// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "util/asio.h"
#include "ReviewIssuanceCreationRequestOpFrame.h"
#include "util/Logging.h"
#include "util/types.h"
#include "database/Database.h"
#include "ledger/LedgerDelta.h"
#include "ledger/ReviewableRequestFrame.h"
#include "ledger/ReferenceFrame.h"
#include "main/Application.h"
#include "xdrpp/printer.h"

namespace stellar
{

using namespace std;
using xdr::operator==;

bool ReviewIssuanceCreationRequestOpFrame::handleApprove(Application & app, LedgerDelta & delta, LedgerManager & ledgerManager, ReviewableRequestFrame::pointer request)
{
	if (request->getRequestType() != ReviewableRequestType::ISSUANCE_CREATE) {
		CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected request type. Expected ISSUANCE_CREATE, but got " << xdr::xdr_traits<ReviewableRequestType>::enum_name(request->getRequestType());
		throw std::invalid_argument("Unexpected request type for review issuance creation request");
	}

	auto issuanceCreationRequest = request->getRequestEntry().body.issuanceRequest();
	Database& db = ledgerManager.getDatabase();
	createReference(delta, db, request->getRequestor(), request->getReference());	

	auto asset = AssetFrame::loadAsset(issuanceCreationRequest.asset, db, &delta);
	if (!asset) {
		CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected state. Expected asset to exist for issuance request. Request: " << xdr::xdr_to_string(request->getRequestEntry());
		throw std::runtime_error("Expected asset for  issuance request to exist");
	}

	if (asset->willExceedMaxIssuanceAmount(issuanceCreationRequest.amount)) {
		innerResult().code(REVIEW_REQUEST_MAX_ISSUANCE_AMOUNT_EXCEEDED);
		return false;
	}

	if (!asset->isAvailableForIssuanceAmountSufficient(issuanceCreationRequest.amount)) {
		innerResult().code(REVIEW_REQUEST_INSUFFICIENT_AVAILABLE_FOR_ISSUANCE_AMOUNT);
		return false;
	}

	if (!asset->tryIssue(issuanceCreationRequest.amount)) {
		CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected state. Failed to fulfill request: " << xdr::xdr_to_string(request->getRequestEntry());
		throw std::runtime_error("Unexcpted issuance result. Expected to be able to issue");
	}

	asset->storeChange(delta, db);

	auto receiver = BalanceFrame::loadBalance(issuanceCreationRequest.receiver, db, &delta);
	if (!receiver) {
		CLOG(ERROR, Logging::OPERATION_LOGGER) << "Excpected receiver to exist: " << xdr::xdr_to_string(request->getRequestEntry());
		throw std::runtime_error("Expected receiver to exist");
	}

	if (!receiver->tryFundAccount(issuanceCreationRequest.amount)) {
		innerResult().code(REVIEW_REQUEST_FULL_LINE);
		return false;
	}

	receiver->storeChange(delta, db);
	
	request->storeDelete(delta, db);
	innerResult().code(REVIEW_REQUEST_SUCCESS);
	return true;
}

bool ReviewIssuanceCreationRequestOpFrame::handleReject(Application & app, LedgerDelta & delta, LedgerManager & ledgerManager, ReviewableRequestFrame::pointer request)
{
	innerResult().code(REVIEW_REQUEST_REJECT_NOT_ALLOWED);
	return false;
}

SourceDetails ReviewIssuanceCreationRequestOpFrame::getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails) const
{
	return SourceDetails({MASTER}, mSourceAccount->getHighThreshold(), SignerType::SIGNER_ASSET_MANAGER);
}

ReviewIssuanceCreationRequestOpFrame::ReviewIssuanceCreationRequestOpFrame(Operation const & op, OperationResult & res, TransactionFrame & parentTx) :
	ReviewRequestOpFrame(op, res, parentTx)
{
}

}
