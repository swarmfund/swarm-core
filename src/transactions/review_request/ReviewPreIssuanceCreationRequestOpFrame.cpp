// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "util/asio.h"
#include "ReviewPreIssuanceCreationRequestOpFrame.h"
#include "util/Logging.h"
#include "util/types.h"
#include "database/Database.h"
#include "ledger/LedgerDelta.h"
#include "ledger/ReviewableRequestFrame.h"
#include "ledger/ReferenceFrame.h"
#include "ledger/AssetHelperLegacy.h"
#include "main/Application.h"

namespace stellar
{

using namespace std;
using xdr::operator==;

bool ReviewPreIssuanceCreationRequestOpFrame::handleApprove(Application & app, LedgerDelta & delta, LedgerManager & ledgerManager, ReviewableRequestFrame::pointer request)
{
	if (request->getRequestType() != ReviewableRequestType::PRE_ISSUANCE_CREATE) {
		CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected request type. Expected PRE_ISSUANCE_CREATE, but got " << xdr::xdr_traits<ReviewableRequestType>::enum_name(request->getRequestType());
		throw std::invalid_argument("Unexpected request type for review preIssuance creation request");
	}

	auto preIssuanceCreationRequest = request->getRequestEntry().body.preIssuanceRequest();
	Database& db = ledgerManager.getDatabase();
	createReference(delta, db, request->getRequestor(), request->getReference());

	auto assetHelper = AssetHelperLegacy::Instance();
	auto asset = assetHelper->loadAsset(preIssuanceCreationRequest.asset, db, &delta);
	if (!asset) {
		CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected state. Expected asset to exist for pre issuance request. RequestID: " << request->getRequestID();
		throw std::runtime_error("Expected asset for pre issuance request to exist");
	}

	if (!asset->tryAddAvailableForIssuance(preIssuanceCreationRequest.amount)) {
		CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected state. Expected to be able to add asset availvle for issuance.";
		throw std::runtime_error("Can not add availalbe for issuance amount");
	}

	EntryHelperProvider::storeChangeEntry(delta, db, asset->mEntry);
	EntryHelperProvider::storeDeleteEntry(delta, db, request->getKey());
	innerResult().code(ReviewRequestResultCode::SUCCESS);
	return true;
}

bool ReviewPreIssuanceCreationRequestOpFrame::handleReject(Application & app, LedgerDelta & delta, LedgerManager & ledgerManager, ReviewableRequestFrame::pointer request)
{
	innerResult().code(ReviewRequestResultCode::REJECT_NOT_ALLOWED);
	return false;
}

SourceDetails ReviewPreIssuanceCreationRequestOpFrame::getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
                                                                               int32_t ledgerVersion) const
{
    auto allowedSigners = static_cast<int32_t>(SignerType::ASSET_MANAGER);

    auto newSingersVersion = static_cast<int32_t>(LedgerVersion::NEW_SIGNER_TYPES);
    if (ledgerVersion >= newSingersVersion)
    {
        allowedSigners = static_cast<int32_t>(SignerType::USER_ISSUANCE_MANAGER);
    }

	return SourceDetails({AccountType::MASTER}, mSourceAccount->getHighThreshold(), allowedSigners);
}

ReviewPreIssuanceCreationRequestOpFrame::ReviewPreIssuanceCreationRequestOpFrame(Operation const & op, OperationResult & res, TransactionFrame & parentTx) :
	ReviewRequestOpFrame(op, res, parentTx)
{
}

}
