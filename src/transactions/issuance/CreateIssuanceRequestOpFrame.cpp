// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include <transactions/review_request/ReviewRequestHelper.h>
#include <ledger/FeeHelper.h>
#include "util/asio.h"
#include "CreateIssuanceRequestOpFrame.h"
#include "ledger/AccountHelper.h"
#include "ledger/AssetHelper.h"
#include "ledger/BalanceHelper.h"
#include "ledger/ReviewableRequestFrame.h"
#include "ledger/ReviewableRequestHelper.h"
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

    const auto reviewResultCode = ReviewRequestHelper::tryApproveRequest(mParentTx, app, ledgerManager, delta, request);
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
    innerResult().success().fee = request->getRequestEntry().body.issuanceRequest().fee;
	auto& db = app.getDatabase();
	auto balanceHelper = BalanceHelper::Instance();
	auto receiver = balanceHelper->mustLoadBalance(mCreateIssuanceRequest.request.receiver, db);
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

	auto reviewableRequestHelper = ReviewableRequestHelper::Instance();
	if (reviewableRequestHelper->isReferenceExist(db, getSourceID(), mCreateIssuanceRequest.reference)) {
		innerResult().code(CreateIssuanceRequestResultCode::REFERENCE_DUPLICATION);
		return nullptr;
	}

	auto assetHelper = AssetHelper::Instance();
	auto asset = assetHelper->loadAsset(mCreateIssuanceRequest.request.asset, db);
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

	auto balanceHelper = BalanceHelper::Instance();
    auto balance = balanceHelper->loadBalance(mCreateIssuanceRequest.request.receiver, db);
	if (!balance || balance->getAsset() != asset->getCode()) {
		innerResult().code(CreateIssuanceRequestResultCode::NO_COUNTERPARTY);
		return nullptr;
	}

    Fee fee = calculateFee(balance->getAccountID(), db);

	auto reference = xdr::pointer<stellar::string64>(new stellar::string64(mCreateIssuanceRequest.reference));
	ReviewableRequestEntry::_body_t body;
	body.type(ReviewableRequestType::ISSUANCE_CREATE);
	body.issuanceRequest() = mCreateIssuanceRequest.request;
    body.issuanceRequest().fee = fee;
	auto request = ReviewableRequestFrame::createNewWithHash(delta, getSourceID(), asset->getOwner(), reference,
                                                             body, ledgerManager.getCloseTime());
	EntryHelperProvider::storeAddEntry(delta, db, request->mEntry);
	return request;
}

Fee CreateIssuanceRequestOpFrame::calculateFee(AccountID receiver, Database &db)
{
    // calculate fee which will be charged from receiver
    Fee fee;
    fee.percent = 0;
    fee.fixed = 0;
    auto receiverFrame = AccountHelper::Instance()->mustLoadAccount(receiver, db);
    if (!isSystemAccountType(receiverFrame->getAccountType()))
    {
        auto feeFrame = FeeHelper::Instance()->loadForAccount(FeeType::EMISSION_FEE, mCreateIssuanceRequest.request.asset,
                                                              FeeFrame::SUBTYPE_ANY, receiverFrame,
                                                              mCreateIssuanceRequest.request.amount, db);
        if (feeFrame) {
            fee.fixed = feeFrame->getFee().fixedFee;
            feeFrame->calculatePercentFee(mCreateIssuanceRequest.request.amount, fee.percent, ROUND_UP);
        }
    }
    if (fee.fixed + fee.percent > mCreateIssuanceRequest.request.amount)
        throw std::runtime_error("Unexpected state. Total fee exceeds issuance amount");

    return fee;
}

}
