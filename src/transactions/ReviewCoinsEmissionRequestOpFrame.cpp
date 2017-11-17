// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "util/asio.h"
#include "transactions/ReviewCoinsEmissionRequestOpFrame.h"
#include "transactions/SignatureValidator.h"
#include "util/Logging.h"
#include "util/types.h"
#include "database/Database.h"
#include "ledger/LedgerDelta.h"
#include "medida/meter.h"
#include "medida/metrics_registry.h"
#include "main/Application.h"
#include "crypto/SHA.h"

// convert from sheep to wheat
// selling sheep
// buying wheat

namespace stellar
{

using namespace std;
using xdr::operator==;

ReviewCoinsEmissionRequestOpFrame::ReviewCoinsEmissionRequestOpFrame(Operation const& op,
                                       OperationResult& res,
                                       TransactionFrame& parentTx)
    : OperationFrame(op, res, parentTx)
    , mReviewCoinsEmissionRequest(mOperation.body.reviewCoinsEmissionRequestOp())
{
}


CoinsEmissionRequestFrame::pointer ReviewCoinsEmissionRequestOpFrame::getRequest(Application& app, LedgerDelta& delta)
{
	Database& db = app.getDatabase();
	bool isManualEmission = mReviewCoinsEmissionRequest.request.requestID == 0;
	if (isManualEmission)
	{
		if (!AssetFrame::exists(db, mReviewCoinsEmissionRequest.request.asset))
		{
			app.getMetrics().NewMeter({ "op-review-coins-emission-request", "invalid", "asset-not-found" },
				"operation").Mark();
			innerResult().code(REVIEW_COINS_EMISSION_REQUEST_ASSET_NOT_FOUND);
			return nullptr;
		}

		if (!BalanceFrame::exists(db, mReviewCoinsEmissionRequest.request.receiver))
		{
			app.getMetrics().NewMeter({ "op-review-coins-emission-request", "invalid", "balance-not-found" },
				"operation").Mark();
			innerResult().code(REVIEW_COINS_EMISSION_REQUEST_BALANCE_NOT_FOUND);
			return nullptr;
		}

		auto requestEntry = mReviewCoinsEmissionRequest.request;
		if (requestEntry.reference.empty())
		{
			uint64_t rawReference = delta.getHeaderFrame().generateID();
			requestEntry.reference = std::to_string(rawReference);
		}

		if (CoinsEmissionRequestFrame::exists(db, requestEntry.issuer, requestEntry.reference))
		{
			app.getMetrics().NewMeter({ "op-review-coins-emission-request", "invalid", "reference-duplication" },
				"operation").Mark();
			innerResult().code(REVIEW_COINS_EMISSION_REQUEST_REFERENCE_DUPLICATION);
			return nullptr;
		}

		return CoinsEmissionRequestFrame::create(requestEntry.amount, requestEntry.asset, requestEntry.reference, 0, requestEntry.issuer, requestEntry.receiver);
	}

	auto request = CoinsEmissionRequestFrame::loadCoinsEmissionRequest(mReviewCoinsEmissionRequest.request.requestID, db, &delta);
	if (!request)
	{
		app.getMetrics().NewMeter({ "op-review-coins-emission-request", "invalid", "not-found" },
			"operation").Mark();
		innerResult().code(REVIEW_COINS_EMISSION_REQUEST_NOT_FOUND);
		return nullptr;
	}

	if (request->getIsApproved())
	{
		app.getMetrics().NewMeter({ "op-review-coins-emission-request", "invalid", "already-reviewed" },
			"operation").Mark();
		innerResult().code(REVIEW_COINS_EMISSION_REQUEST_ALREADY_REVIEWED);
		return nullptr;
	}

	if (!(request->getCoinsEmissionRequest() == mReviewCoinsEmissionRequest.request))
	{
		app.getMetrics().NewMeter({ "op-review-coins-emission-request", "invalid", "invalid-emission-id" },
			"operation").Mark();
		innerResult().code(REVIEW_COINS_EMISSION_REQUEST_NOT_EQUAL);
		return nullptr;
	}

	return request;
}

std::unordered_map<AccountID, CounterpartyDetails> ReviewCoinsEmissionRequestOpFrame::getCounterpartyDetails(Database & db, LedgerDelta * delta) const
{
	// it's possible to reject any request
	if (!mReviewCoinsEmissionRequest.approve)
		return{};
	return {
		// we do not care about receiver
		{mReviewCoinsEmissionRequest.request.issuer, CounterpartyDetails({MASTER}, false, true) },
	};
}

SourceDetails ReviewCoinsEmissionRequestOpFrame::getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails) const
{
	return SourceDetails({ MASTER }, mSourceAccount->getMediumThreshold(), SIGNER_EMISSION_MANAGER);
}

bool
ReviewCoinsEmissionRequestOpFrame::doApply(Application& app,
                            LedgerDelta& delta, LedgerManager& ledgerManager)
{

	auto request = getRequest(app, delta);
	if (!request)
	{
		return false;
	}

	Database& db = ledgerManager.getDatabase();
	if (!mReviewCoinsEmissionRequest.approve)
	{
		request->storeDelete(delta, db);
		innerResult().code(REVIEW_COINS_EMISSION_REQUEST_SUCCESS);
		app.getMetrics().NewMeter({ "op-review-coins-emission-request", "success", "apply" },
			"operation").Mark();
		return true;
	}

	int64_t preEmissionRequired = mReviewCoinsEmissionRequest.request.amount;
	assert(preEmissionRequired > 0);
	auto masterBalanceFrame = BalanceFrame::loadBalance(app.getMasterID(),
		mReviewCoinsEmissionRequest.request.asset,
		app.getDatabase(), &delta);
	assert(masterBalanceFrame);
	if (masterBalanceFrame->getBalanceID() == mReviewCoinsEmissionRequest.request.receiver)
	{
		app.getMetrics().NewMeter({ "op-review-coins-emission-request", "invalid", "receiver-can-not-be-master-account" },
			"operation").Mark();
		innerResult().code(REVIEW_COINS_EMISSION_REQUEST_MALFORMED);
		return false;
	}

	if (masterBalanceFrame->getAmount() < preEmissionRequired)
	{
		app.getMetrics().NewMeter({ "op-review-coins-emission-request", "invalid", "not-enough-preemissions" },
			"operation").Mark();
		innerResult().code(REVIEW_COINS_EMISSION_REQUEST_NOT_ENOUGH_PREEMISSIONS);
		return false;
	}
	assert(masterBalanceFrame->addBalance(-preEmissionRequired));
	masterBalanceFrame->storeChange(delta, db);

	auto balanceFrame = BalanceFrame::loadBalance(mReviewCoinsEmissionRequest.request.receiver, app.getDatabase(), &delta);
	if (!balanceFrame)
		throw std::runtime_error("Invalid db state. Failed to load balance for emission request");

	if (!(balanceFrame->getAsset() == masterBalanceFrame->getAsset()) || !(balanceFrame->getAsset() == mReviewCoinsEmissionRequest.request.asset))
	{
		app.getMetrics().NewMeter({ "op-review-coins-emission-request", "invalid", "asset-mistmatched" },
			"operation").Mark();
		innerResult().code(REVIEW_COINS_EMISSION_REQUEST_MALFORMED);
		return false;
	}

	if (!balanceFrame->addBalance(mReviewCoinsEmissionRequest.request.amount))
	{
		app.getMetrics().NewMeter({ "op-review-coins-emission-request", "invalid", "balance-is-full" },
			"operation").Mark();
		innerResult().code(REVIEW_COINS_EMISSION_REQUEST_LINE_FULL);
		return false;
	}

	balanceFrame->storeChange(delta, db);


	request->setIsApproved(true);
	bool isManualEmission = request->getID() == 0; 
	if (isManualEmission)
	{
		request->getCoinsEmissionRequest().requestID = delta.getHeaderFrame().generateID();
		request->storeAdd(delta, db);
	}
	else {
		request->storeChange(delta, db);
	}
	innerResult().success().requestID = request->getID();

	innerResult().code(REVIEW_COINS_EMISSION_REQUEST_SUCCESS);
	app.getMetrics().NewMeter({ "op-review-coins-emission-request", "success", "apply" },
		"operation").Mark();
	return true;
}

// makes sure the currencies are different
bool
ReviewCoinsEmissionRequestOpFrame::doCheckValid(Application& app)
{

    if (mReviewCoinsEmissionRequest.request.amount <= 0 ||
        !isAssetValid(mReviewCoinsEmissionRequest.request.asset))
    {
        app.getMetrics().NewMeter({"op-review-coins-emission-request", "invalid", "invalid-request"},
                         "operation").Mark();
		innerResult().code(REVIEW_COINS_EMISSION_REQUEST_MALFORMED);
        return false;
    }


    bool manualEmission = mReviewCoinsEmissionRequest.request.requestID == 0;
    // requestID could be 0 only in case of manual emission
    if (!mReviewCoinsEmissionRequest.approve && manualEmission)
	{
		app.getMetrics().NewMeter({ "op-review-coins-emission-request", "invalid", "invalid-request-id" },
			"operation").Mark();
		innerResult().code(REVIEW_COINS_EMISSION_REQUEST_MALFORMED);
		return false;
	}

	if (manualEmission && !(mReviewCoinsEmissionRequest.request.issuer == getSourceID()))
	{
		app.getMetrics().NewMeter({ "op-review-coins-emission-request", "invalid", "invalid-issuer" },
			"operation").Mark();
		innerResult().code(REVIEW_COINS_EMISSION_REQUEST_MALFORMED);
		return false;
	}

    if (mReviewCoinsEmissionRequest.request.amount <= 0)
	{
		app.getMetrics().NewMeter({ "op-review-coins-emission-request", "invalid", "invalid-amount" },
			"operation").Mark();
		innerResult().code(REVIEW_COINS_EMISSION_REQUEST_MALFORMED);
		return false;
	}

	if (mReviewCoinsEmissionRequest.approve && mReviewCoinsEmissionRequest.reason.size() != 0)
	{
		app.getMetrics().NewMeter({ "op-review-coins-emission-request", "invalid", "invalid-reason" },
			"operation").Mark();
		innerResult().code(REVIEW_COINS_EMISSION_REQUEST_INVALID_REASON);
		return false;
	}

    return true;
}

}
