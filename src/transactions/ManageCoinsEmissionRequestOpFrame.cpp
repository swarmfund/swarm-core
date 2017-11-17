// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "util/asio.h"
#include "transactions/ManageCoinsEmissionRequestOpFrame.h"
#include "ledger/CoinsEmissionFrame.h"
#include "util/Logging.h"
#include "util/types.h"
#include "database/Database.h"
#include "ledger/LedgerDelta.h"
#include "medida/meter.h"
#include "medida/metrics_registry.h"
#include "main/Application.h"
#include "crypto/SHA.h"
#include "crypto/Hex.h"

namespace stellar
{

using namespace std;
using xdr::operator==;

std::unordered_map<AccountID, CounterpartyDetails> ManageCoinsEmissionRequestOpFrame::getCounterpartyDetails(Database & db, LedgerDelta * delta) const
{
	auto targetBalance = BalanceFrame::loadBalance(mManageCoinsEmissionRequest.receiver, db, delta);
	// counterparty does not exists, error will be returned from do apply
	if (!targetBalance)
		return{};
	return{
		{ targetBalance->getAccountID(), CounterpartyDetails({ GENERAL, NOT_VERIFIED}, true, false) }
	};
}

SourceDetails ManageCoinsEmissionRequestOpFrame::getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails) const
{
	return SourceDetails({MASTER}, mSourceAccount->getHighThreshold(), SIGNER_EMISSION_MANAGER);
}

ManageCoinsEmissionRequestOpFrame::ManageCoinsEmissionRequestOpFrame(Operation const& op,
                                       OperationResult& res,
                                       TransactionFrame& parentTx)
    : OperationFrame(op, res, parentTx)
    , mManageCoinsEmissionRequest(mOperation.body.manageCoinsEmissionRequestOp())
{
}

bool
ManageCoinsEmissionRequestOpFrame::doApply(Application& app,
                            LedgerDelta& delta, LedgerManager& ledgerManager)
{
    Database& db = ledgerManager.getDatabase();

	innerResult().code(MANAGE_COINS_EMISSION_REQUEST_SUCCESS);
	// create new
	if (mManageCoinsEmissionRequest.action == MANAGE_COINS_EMISSION_REQUEST_CREATE)
    {
        if (CoinsEmissionRequestFrame::exists(db, getSourceID(), mManageCoinsEmissionRequest.reference))
        {
            app.getMetrics().NewMeter({ "op-manage-coins-emission-request", "invalid", "reference-duplication" },
                "operation").Mark();
            innerResult().code(MANAGE_COINS_EMISSION_REQUEST_REFERENCE_DUPLICATION);
            return false;
        }

        if (!AssetFrame::exists(db, mManageCoinsEmissionRequest.asset)) 
        {
            app.getMetrics().NewMeter({ "op-manage-coins-emission-request", "invalid", "asset-not-found" },
                "operation").Mark();
            innerResult().code(MANAGE_COINS_EMISSION_REQUEST_ASSET_NOT_FOUND);
            return false;
        }

        auto balance = BalanceFrame::loadBalance(mManageCoinsEmissionRequest.receiver, db);
        
        if (!balance) 
        {
            app.getMetrics().NewMeter({ "op-manage-coins-emission-request", "invalid", "balance-not-found" },
                "operation").Mark();
            innerResult().code(MANAGE_COINS_EMISSION_REQUEST_BALANCE_NOT_FOUND);
            return false;
        }
        
        if (balance->getAsset() != mManageCoinsEmissionRequest.asset)
        {
            app.getMetrics().NewMeter({ "op-manage-coins-emission-request", "invalid", "asset-mismatch" },
                "operation").Mark();
            innerResult().code(MANAGE_COINS_EMISSION_REQUEST_ASSET_MISMATCH);
            return false;
        }

		if (mManageCoinsEmissionRequest.amount < 0)
		{
			app.getMetrics().NewMeter({ "op-manage-coins-emission-request", "invalid", "invalid-amount" },
				"operation").Mark();
			innerResult().code(MANAGE_COINS_EMISSION_REQUEST_INVALID_AMOUNT);
			return false;
		}

		auto emissionRequest = tryCreateEmissionRequest(app, db, delta, ledgerManager, mManageCoinsEmissionRequest.amount, mManageCoinsEmissionRequest.asset,
			mManageCoinsEmissionRequest.reference, mSourceAccount->getID(), balance);
        
		if (!emissionRequest)
		{
			app.getMetrics().NewMeter({ "op-manage-coins-emission-request", "invalid", "full-line" },
				"operation").Mark();
			innerResult().code(MANAGE_COINS_EMISSION_REQUEST_LINE_FULL);
			return false;
		}

		innerResult().manageRequestInfo().fulfilled = emissionRequest->getCoinsEmissionRequest().isApproved;

        innerResult().manageRequestInfo().requestID = emissionRequest->getID();
        app.getMetrics().NewMeter({ "op-manage-coins-emission-request", "success", "apply" },
            "operation").Mark();
        return true;
    }
    else if (mManageCoinsEmissionRequest.action == MANAGE_COINS_EMISSION_REQUEST_DELETE)
    {
        auto requestFrame = CoinsEmissionRequestFrame::loadCoinsEmissionRequest(mManageCoinsEmissionRequest.requestID,
            db, &delta);
        if (!requestFrame)
        {
            app.getMetrics().NewMeter({ "op-manage-coins-emission-request", "invalid", "not-found" },
                "operation").Mark();
            innerResult().code(MANAGE_COINS_EMISSION_REQUEST_NOT_FOUND);
            return false;
        }

        if (requestFrame->getIsApproved())
        {
            app.getMetrics().NewMeter({ "op-manage-coins-emission-request", "invalid", "already-reviewed" },
                "operation").Mark();
            innerResult().code(MANAGE_COINS_EMISSION_REQUEST_ALREADY_REVIEWED);
            return false;
        }

        requestFrame->storeDelete(delta, db);
		app.getMetrics().NewMeter({ "op-manage-coins-emission-request", "success", "apply" },
			"operation").Mark();
		return true;
    }
	return true;
}


bool ManageCoinsEmissionRequestOpFrame::emitTokens(Application& app, Database& db, LedgerDelta& delta, LedgerManager& ledgerManager, AccountFrame::pointer destAccount, AccountID& issuer, int64_t  amount, AssetCode& token, EmissionFeeType emissionFeeType) {
	int64_t commissionReceive = amount;

	// try get token emission rules for dest account
	assert(destAccount);
	auto tokenRules = FeeFrame::loadForAccount(EMISSION_FEE, token, int64_t(emissionFeeType),
		destAccount, amount, db);
	if (tokenRules && tokenRules->getPercentFee() != 0)
	{
		auto tokenBalance = BalanceFrame::loadBalance(destAccount->getAccount().accountID, token, db, &delta);
		if (!tokenBalance)
		{
			tokenBalance = BalanceFrame::createNew(BalanceKeyUtils::forAccount(destAccount->getID(), delta.getHeaderFrame().generateID()), destAccount->getID(), token, ledgerManager.getCloseTime(), 0);
			tokenBalance->storeAdd(delta, db);
		}

		int64_t clientReceive = tokenRules->calculatePercentFee(amount, false);
		assert(commissionReceive >= clientReceive);
		commissionReceive -= clientReceive;

		if (clientReceive > 0)
		{
			std::string reference = std::to_string(delta.getHeaderFrame().generateID());
			auto mainRequestFrame = ManageCoinsEmissionRequestOpFrame::tryCreateEmissionRequest(app, db, delta, ledgerManager, clientReceive, token,
				reference, issuer, tokenBalance);
			if (!mainRequestFrame)
				return false;
		}
	}

	auto operationalBalanceFrame = BalanceFrame::loadBalance(app.getOperationalID(), token, db, &delta);
	assert(operationalBalanceFrame);

	std::string reference = std::to_string(delta.getHeaderFrame().generateID());
	auto operationalRequestFrame = ManageCoinsEmissionRequestOpFrame::tryCreateEmissionRequest(app, db, delta, ledgerManager, commissionReceive, token,
		reference, issuer, operationalBalanceFrame);
	if (!operationalRequestFrame)
		return false;

	return true;
}

CoinsEmissionRequestFrame::pointer
ManageCoinsEmissionRequestOpFrame::tryCreateEmissionRequest(Application& app, Database& db, LedgerDelta& delta, LedgerManager& ledgerManager, int64_t amount, AssetCode asset, std::string reference, AccountID issuer,
	BalanceFrame::pointer receiver)
{
	AccountID receiverID = receiver->getAccountID();
	if (delta.getHeaderFrame().useEmissionRequestBalanceID())
		receiverID = receiver->getBalanceID();
	auto requestFrame = CoinsEmissionRequestFrame::create(amount, asset, reference, delta.getHeaderFrame().generateID(), issuer, receiverID);

	uint64 preEmissionRequired = amount;
	auto masterBalanceFrame = BalanceFrame::loadBalance(app.getMasterID(),
		asset,
		app.getDatabase(), &delta);
	if (masterBalanceFrame->getAmount() >= preEmissionRequired)
	{
		auto account = AccountFrame::loadAccount(delta, receiver->getAccountID(), db);
		assert(account);
		auto masterAccount = AccountFrame::loadAccount(delta, app.getMasterID(), db);
		assert(masterAccount);

		if (!receiver->addBalance(amount))
		{
			return nullptr;
		}
		receiver->storeChange(delta, db);
		requestFrame->getCoinsEmissionRequest().isApproved = true;

		assert(masterBalanceFrame->addBalance(-amount));
		masterBalanceFrame->storeChange(delta, db);
	}

	requestFrame->storeAdd(delta, db);
	return requestFrame;
}

bool
ManageCoinsEmissionRequestOpFrame::doCheckValid(Application& app)
{

    if (!isAssetValid(mManageCoinsEmissionRequest.asset))
    {
		app.getMetrics().NewMeter({ "op-manage-coins-emission-request", "invalid", "invalid-asset" },
			"operation").Mark();
		innerResult().code(MANAGE_COINS_EMISSION_REQUEST_INVALID_ASSET);
		return false;
    }

    if ((mManageCoinsEmissionRequest.amount <= 0)
        && mManageCoinsEmissionRequest.action == MANAGE_COINS_EMISSION_REQUEST_CREATE)
    {
        app.getMetrics().NewMeter({"op-manage-coins-emission-request", "invalid", "invalid-amount"},
                         "operation").Mark();
        innerResult().code(MANAGE_COINS_EMISSION_REQUEST_INVALID_AMOUNT);
        return false;
    }
    if (mManageCoinsEmissionRequest.requestID != 0 && mManageCoinsEmissionRequest.action == MANAGE_COINS_EMISSION_REQUEST_CREATE)
    {
		app.getMetrics().NewMeter({ "op-manage-coins-emission-request", "invalid", "invalid-emission-id" },
			"operation").Mark();
		innerResult().code(MANAGE_COINS_EMISSION_REQUEST_INVALID_REQUEST_ID);
		return false;
    }

    if (mManageCoinsEmissionRequest.requestID == 0 && mManageCoinsEmissionRequest.action == MANAGE_COINS_EMISSION_REQUEST_DELETE)
    {
		app.getMetrics().NewMeter({ "op-manage-coins-emission-request", "invalid", "invalid-emission-id" },
			"operation").Mark();
		innerResult().code(MANAGE_COINS_EMISSION_REQUEST_INVALID_REQUEST_ID);
		return false;
    }

    if (mManageCoinsEmissionRequest.reference.size() == 0 && mManageCoinsEmissionRequest.action == MANAGE_COINS_EMISSION_REQUEST_CREATE)
    {
		app.getMetrics().NewMeter({ "op-manage-coins-emission-request", "invalid", "invalid-reference" },
			"operation").Mark();
		innerResult().code(MANAGE_COINS_EMISSION_REQUEST_INVALID_REFERENCE);
		return false;
    }


    return true;
}

}
