// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "util/asio.h"
#include "ReviewAssetCreationRequestOpFrame.h"
#include "util/Logging.h"
#include "util/types.h"
#include "database/Database.h"
#include "ledger/LedgerDelta.h"
#include "ledger/ReviewableRequestFrame.h"
#include "ledger/AssetFrame.h"
#include "main/Application.h"

namespace stellar
{

using namespace std;
using xdr::operator==;

bool ReviewAssetCreationRequestOpFrame::handleApprove(Application & app, LedgerDelta & delta, LedgerManager & ledgerManager, ReviewableRequestFrame::pointer request)
{
	if (request->getRequestType() != ReviewableRequestType::ASSET_CREATE) {
		CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected request type. Expected ASSET_CREATE, but got " << xdr::xdr_traits<ReviewableRequestType>::enum_name(request->getRequestType());
		throw std::invalid_argument("Unexpected request type for review asset creation request");
	}

	auto assetCreationRequest = request->getRequestEntry().body.assetCreationRequest();
	Database& db = ledgerManager.getDatabase();
	auto isAssetExist = AssetFrame::exists(db, assetCreationRequest.code);
	if (isAssetExist) {
		innerResult().code(REVIEW_REQUEST_ASSET_ALREADY_EXISTS);
		return false;
	}

	auto assetFrame = AssetFrame::create(assetCreationRequest, request->getRequestor());
	assetFrame->storeAdd(delta, db);
	request->storeDelete(delta, db);
	innerResult().code(REVIEW_REQUEST_SUCCESS);
	return true;
}

SourceDetails ReviewAssetCreationRequestOpFrame::getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails) const
{
	return SourceDetails({MASTER}, mSourceAccount->getHighThreshold(), SignerType::SIGNER_ASSET_MANAGER);
}

ReviewAssetCreationRequestOpFrame::ReviewAssetCreationRequestOpFrame(Operation const & op, OperationResult & res, TransactionFrame & parentTx) :
	ReviewRequestOpFrame(op, res, parentTx)
{
}

}
