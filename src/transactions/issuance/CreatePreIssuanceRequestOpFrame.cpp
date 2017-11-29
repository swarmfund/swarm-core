// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "util/asio.h"
#include "CreatePreIssuanceRequestOpFrame.h"
#include "transactions/SignatureValidator.h"
#include "ledger/ReviewableRequestFrame.h"
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
	if (ReviewableRequestFrame::isReferenceExist(db, getSourceID(), mCreatePreIssuanceRequest.request.reference)) {
		innerResult().code(CREATE_PREISSUANCE_REQUEST_REFERENCE_DUPLICATION);
		return false;
	}

	auto asset = AssetFrame::loadAsset(mCreatePreIssuanceRequest.request.asset, db);
	if (!asset) {
		innerResult().code(CREATE_PREISSUANCE_REQUEST_ASSET_NOT_FOUND);
		return false;
	}

	if (!(asset->getOwner() == getSourceID())) {
		innerResult().code(CREATE_PREISSUANCE_REQUEST_NOT_AUTHORIZED_UPLOAD);
		return false;
	}

	if (!isSignatureValid(asset)) {
		innerResult().code(CREATE_PREISSUANCE_REQUEST_INVALID_SIGNATURE);
		return false;
	}

	if (!asset->canAddAvailableForIssuance(mCreatePreIssuanceRequest.request.amount)) {
		innerResult().code(CREATE_PREISSUANCE_REQUEST_EXCEEDED_MAX_AMOUNT);
		return false;
	}

	auto reference = xdr::pointer<stellar::string64>(new stellar::string64(mCreatePreIssuanceRequest.request.reference));
	ReviewableRequestEntry::_body_t requestBody;
	requestBody.type(ReviewableRequestType::PRE_ISSUANCE_CREATE);
	requestBody.preIssuanceRequest() = mCreatePreIssuanceRequest.request;
	auto request = ReviewableRequestFrame::createNewWithHash(delta.getHeaderFrame().generateID(), getSourceID(), app.getMasterID(), reference, requestBody);
	request->storeAdd(delta, db);
	innerResult().code(CREATE_PREISSUANCE_REQUEST_SUCCESS);
	innerResult().success().requestID = request->getRequestID();
	return true;
}

bool
CreatePreIssuanceRequestOpFrame::doCheckValid(Application& app)
{
    
    if (!AssetFrame::isAssetCodeValid(mCreatePreIssuanceRequest.request.asset)) {
        innerResult().code(CREATE_PREISSUANCE_REQUEST_ASSET_NOT_FOUND);
        return false;
    }

	if (mCreatePreIssuanceRequest.request.amount == 0) {
		innerResult().code(CREATE_PREISSUANCE_REQUEST_INVALID_AMOUNT);
		return false;
	}

	if (mCreatePreIssuanceRequest.request.reference.empty()) {
		innerResult().code(CREATE_PREISSUANCE_REQUEST_INVALID_REFERENCE);
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

SourceDetails CreatePreIssuanceRequestOpFrame::getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails) const
{
	return SourceDetails({MASTER, SYNDICATE}, mSourceAccount->getHighThreshold(), SIGNER_ISSUANCE_MANAGER);
}

bool CreatePreIssuanceRequestOpFrame::isSignatureValid(AssetFrame::pointer asset)
{
	auto& request = mCreatePreIssuanceRequest.request;
	auto signatureData = getSignatureData(mCreatePreIssuanceRequest.request.reference, request.amount, request.asset);
	auto signatureValidator = SignatureValidator(signatureData, { request.signature });

	const int VALID_SIGNATURES_REQUIRED = 1;
	SignatureValidator::Result result = signatureValidator.check({ asset->getPreIssuedAssetSigner() }, VALID_SIGNATURES_REQUIRED);
	return result == SignatureValidator::Result::SUCCESS;
}

}
