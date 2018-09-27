// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include <transactions/review_request/ReviewRequestHelper.h>
#include "util/asio.h"
#include "CreatePreIssuanceRequestOpFrame.h"
#include "transactions/SignatureValidatorImpl.h"
#include "ledger/AssetHelperLegacy.h"
#include "ledger/ReviewableRequestFrame.h"
#include "ledger/ReviewableRequestHelper.h"
#include "ledger/ReferenceFrame.h"
#include "util/Logging.h"
#include "util/types.h"
#include "database/Database.h"
#include "ledger/LedgerDelta.h"
#include "main/Application.h"
#include "crypto/SHA.h"

namespace stellar
{

using namespace std;
using xdr::operator==;

CreatePreIssuanceRequestOpFrame::CreatePreIssuanceRequestOpFrame(Operation const& op,
                                       OperationResult& res,
                                       TransactionFrame& parentTx)
    : OperationFrame(op, res, parentTx)
    , mCreatePreIssuanceRequest(mOperation.body.createPreIssuanceRequest())
{
}

bool
CreatePreIssuanceRequestOpFrame::doApply(Application& app,
                            LedgerDelta& delta, LedgerManager& ledgerManager)
{
    Database& db = ledgerManager.getDatabase();

	auto reviewableRequestHelper = ReviewableRequestHelper::Instance();
	if (reviewableRequestHelper->isReferenceExist(db, getSourceID(), mCreatePreIssuanceRequest.request.reference)) {
		innerResult().code(CreatePreIssuanceRequestResultCode::REFERENCE_DUPLICATION);
		return false;
	}

	auto assetHelper = AssetHelperLegacy::Instance();
	auto asset = assetHelper->loadAsset(mCreatePreIssuanceRequest.request.asset, db);
	if (!asset) {
		innerResult().code(CreatePreIssuanceRequestResultCode::ASSET_NOT_FOUND);
		return false;
	}

	if (!(asset->getOwner() == getSourceID())) {
		innerResult().code(CreatePreIssuanceRequestResultCode::NOT_AUTHORIZED_UPLOAD);
		return false;
	}

	if (!isSignatureValid(asset, LedgerVersion(ledgerManager.getCurrentLedgerHeader().ledgerVersion))) {
		innerResult().code(CreatePreIssuanceRequestResultCode::INVALID_SIGNATURE);
		return false;
	}

	if (!asset->canAddAvailableForIssuance(mCreatePreIssuanceRequest.request.amount)) {
		innerResult().code(CreatePreIssuanceRequestResultCode::EXCEEDED_MAX_AMOUNT);
		return false;
	}

	auto reference = xdr::pointer<stellar::string64>(new stellar::string64(mCreatePreIssuanceRequest.request.reference));
	ReviewableRequestEntry::_body_t requestBody;
	requestBody.type(ReviewableRequestType::PRE_ISSUANCE_CREATE);
	requestBody.preIssuanceRequest() = mCreatePreIssuanceRequest.request;
	auto request = ReviewableRequestFrame::createNewWithHash(delta, getSourceID(), app.getMasterID(), reference,
                                                             requestBody, ledgerManager.getCloseTime());
	EntryHelperProvider::storeAddEntry(delta, db, request->mEntry);

    //if source is master then auto review
    bool isFulfilled = false;
    if (getSourceAccount().getAccountType() == AccountType::MASTER) {
        auto result = ReviewRequestHelper::tryApproveRequest(mParentTx, app, ledgerManager, delta, request);
        if (result != ReviewRequestResultCode::SUCCESS) {
            CLOG(ERROR, Logging::OPERATION_LOGGER) << "Failed to approve request: " << request->getRequestID();
            throw std::runtime_error("Failed to approve request");
        }
        isFulfilled = true;
    }
	innerResult().code(CreatePreIssuanceRequestResultCode::SUCCESS);
	innerResult().success().requestID = request->getRequestID();
    innerResult().success().fulfilled = isFulfilled;
	return true;
}

bool
CreatePreIssuanceRequestOpFrame::doCheckValid(Application& app)
{
    
    if (!AssetFrame::isAssetCodeValid(mCreatePreIssuanceRequest.request.asset)) {
        innerResult().code(CreatePreIssuanceRequestResultCode::ASSET_NOT_FOUND);
        return false;
    }

	if (mCreatePreIssuanceRequest.request.amount == 0) {
		innerResult().code(CreatePreIssuanceRequestResultCode::INVALID_AMOUNT);
		return false;
	}

	if (mCreatePreIssuanceRequest.request.reference.empty()) {
		innerResult().code(CreatePreIssuanceRequestResultCode::INVALID_REFERENCE);
		return false;
	}
	
    return true;
}

Hash CreatePreIssuanceRequestOpFrame::getSignatureData(stellar::string64 const & reference, uint64_t const & amount, AssetCode const & assetCode)
{
	std::string rawSignatureData = reference + ":" + std::to_string(amount) + ":" + assetCode;
	return Hash(sha256(rawSignatureData));
}

std::unordered_map<AccountID, CounterpartyDetails> CreatePreIssuanceRequestOpFrame::getCounterpartyDetails(Database & db, LedgerDelta * delta) const
{
	// no counterparties
	return{};
}

SourceDetails CreatePreIssuanceRequestOpFrame::getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
                                                                       int32_t ledgerVersion) const
{
	return SourceDetails({AccountType::MASTER, AccountType::SYNDICATE}, mSourceAccount->getHighThreshold(),
						 static_cast<int32_t>(SignerType::ISSUANCE_MANAGER));
}

bool CreatePreIssuanceRequestOpFrame::isSignatureValid(AssetFrame::pointer asset, LedgerVersion version)
{
	auto& request = mCreatePreIssuanceRequest.request;
	auto signatureData = getSignatureData(mCreatePreIssuanceRequest.request.reference, request.amount, request.asset);
	auto signatureValidator = SignatureValidatorImpl(signatureData, { request.signature });

	const int VALID_SIGNATURES_REQUIRED = 1;
	SignatureValidator::Result result = signatureValidator.check({ asset->getPreIssuedAssetSigner() }, VALID_SIGNATURES_REQUIRED, version);
	return result == SignatureValidator::Result::SUCCESS;
}

}
