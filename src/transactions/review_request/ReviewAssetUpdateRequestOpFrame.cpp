// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include <transactions/manage_asset/ManageAssetHelper.h>
#include "ReviewAssetUpdateRequestOpFrame.h"
#include "database/Database.h"
#include "ledger/AssetHelperLegacy.h"
#include "ledger/BalanceHelperLegacy.h"
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

	auto assetHelper = AssetHelperLegacy::Instance();
	auto assetFrame = assetHelper->loadAsset(assetUpdateRequest.code, db, &delta);
	if (!assetFrame) {
		innerResult().code(ReviewRequestResultCode::ASSET_DOES_NOT_EXISTS);
		return false;
	}

	if (!(assetFrame->getOwner() == request->getRequestor())) {
		CLOG(ERROR, Logging::OPERATION_LOGGER) << "Received approval for update request not initiated by owner of asset." <<
			"request id: " << request->getRequestID();
		throw std::runtime_error("Received approval for update request not initiated by owner of asset.");
	}
    bool wasBase = assetFrame->isPolicySet(AssetPolicy::BASE_ASSET);
    
	AssetEntry& assetEntry = assetFrame->getAsset();
	assetEntry.details = assetUpdateRequest.details;
	assetEntry.policies = assetUpdateRequest.policies;
    
	EntryHelperProvider::storeChangeEntry(delta, db, assetFrame->mEntry);
    bool isBase = assetFrame->isPolicySet(AssetPolicy::BASE_ASSET);
    
    if (!wasBase && isBase)
        ManageAssetHelper::createSystemBalances(assetFrame->getCode(), app, delta);
    
	EntryHelperProvider::storeDeleteEntry(delta, db, request->getKey());
	innerResult().code(ReviewRequestResultCode::SUCCESS);
	return true;
}

SourceDetails ReviewAssetUpdateRequestOpFrame::getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
                                                                       int32_t ledgerVersion) const
{
    auto allowedSigners = static_cast<int32_t>(SignerType::ASSET_MANAGER);

    auto newSingersVersion = static_cast<int32_t>(LedgerVersion::NEW_SIGNER_TYPES);
    if (ledgerVersion >= newSingersVersion)
    {
        allowedSigners = static_cast<int32_t>(SignerType::USER_ASSET_MANAGER);
    }

	return SourceDetails({AccountType::MASTER}, mSourceAccount->getHighThreshold(), allowedSigners);
}

ReviewAssetUpdateRequestOpFrame::ReviewAssetUpdateRequestOpFrame(Operation const & op, OperationResult & res, TransactionFrame & parentTx) :
	ReviewRequestOpFrame(op, res, parentTx)
{
}

}
