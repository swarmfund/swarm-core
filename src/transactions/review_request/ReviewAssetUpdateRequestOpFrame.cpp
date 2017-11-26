// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "util/asio.h"
#include "ReviewAssetUpdateRequestOpFrame.h"
#include "util/Logging.h"
#include "util/types.h"
#include "database/Database.h"
#include "ledger/LedgerDelta.h"
#include "ledger/ReviewableRequestFrame.h"
#include "ledger/AssetFrame.h"
#include "main/Application.h"
#include "lib/util/format.h"

namespace stellar
{

using namespace std;
using xdr::operator==;

bool ReviewAssetUpdateRequestOpFrame::handleApprove(Application & app, LedgerDelta & delta, LedgerManager & ledgerManager, ReviewableRequestFrame::pointer request)
{
	if (request->getRequestType() != ReviewableRequestType::ASSET_UPDATE) {
		CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected request type. Expected ASSET_UPDATE, but got " << xdr::xdr_traits<ReviewableRequestType>::enum_name(request->getRequestType());
		throw std::invalid_argument("Unexpected request type for review asset update request");
	}

	auto assetUpdateRequest = request->getRequestEntry().body.assetUpdateRequest();
	Database& db = ledgerManager.getDatabase();
	auto assetFrame = AssetFrame::loadAsset(assetUpdateRequest.code, db, &delta);
	if (!assetFrame) {
		innerResult().code(REVIEW_REQUEST_ASSET_DOES_NOT_EXISTS);
		return false;
	}

	if (!(assetFrame->getOwner() == request->getRequestor())) {
		CLOG(ERROR, Logging::OPERATION_LOGGER) << "Received approval for update request not initiated by owner of asset." <<
			"request id: " << request->getRequestID();
		throw std::runtime_error("Received approval for update request not initiated by owner of asset.");
	}

	AssetEntry& assetEntry = assetFrame->getAsset();
	assetEntry.description = assetUpdateRequest.description;
	assetEntry.externalResourceLink = assetUpdateRequest.externalResourceLink;
	assetEntry.policies = assetUpdateRequest.policies;
	assetFrame->storeChange(delta, db);
	request->storeDelete(delta, db);
	innerResult().code(REVIEW_REQUEST_SUCCESS);
	return true;
}

SourceDetails ReviewAssetUpdateRequestOpFrame::getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails) const
{
	return SourceDetails({MASTER}, mSourceAccount->getHighThreshold(), SignerType::SIGNER_ASSET_MANAGER);
}

ReviewAssetUpdateRequestOpFrame::ReviewAssetUpdateRequestOpFrame(Operation const & op, OperationResult & res, TransactionFrame & parentTx) :
	ReviewRequestOpFrame(op, res, parentTx)
{
}

}
