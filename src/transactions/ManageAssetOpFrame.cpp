// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "transactions/ManageAssetOpFrame.h"
#include "ledger/LedgerDelta.h"
#include "ledger/AssetPairFrame.h"
#include "ledger/FeeFrame.h"

#include "database/Database.h"

#include "main/Application.h"
#include "medida/meter.h"
#include "medida/metrics_registry.h"

namespace stellar
{

using namespace std;
using xdr::operator==;
    
ManageAssetOpFrame::ManageAssetOpFrame(Operation const& op,
                                           OperationResult& res,
                                           TransactionFrame& parentTx)
    : OperationFrame(op, res, parentTx)
    , mManageAsset(mOperation.body.manageAssetOp())
{
}

std::unordered_map<AccountID, CounterpartyDetails> ManageAssetOpFrame::getCounterpartyDetails(Database & db, LedgerDelta * delta) const
{
	// no counterparties
	return std::unordered_map<AccountID, CounterpartyDetails>();
}

SourceDetails ManageAssetOpFrame::getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails) const
{
	return SourceDetails({MASTER}, mSourceAccount->getHighThreshold(), SIGNER_ASSET_MANAGER);
}

bool ManageAssetOpFrame::createAsset(AssetFrame::pointer assetFrame, Application& app, LedgerManager& ledgerManager, Database& db, LedgerDelta& delta)
{
	assetFrame->storeAdd(delta, db);
	auto systemAccounts = app.getSystemAccounts();
	for (auto systemAccountID : systemAccounts)
	{
		uint64_t balanceID = delta.getHeaderFrame().generateID();
		auto balanceFrame = BalanceFrame::createNew(BalanceKeyUtils::forAccount(systemAccountID, balanceID), systemAccountID,
			 assetFrame->getCode(), ledgerManager.getCloseTime());
		balanceFrame->storeAdd(delta, db);
	}
	
	auto assetPair = AssetPairFrame::create(assetFrame->getCode(), app.getStatsQuoteAsset(), ONE, ONE, 0, 0, 0);
	assetPair->storeAdd(delta, db);
	return true;
}

bool
ManageAssetOpFrame::doApply(Application& app,
                              LedgerDelta& delta, LedgerManager& ledgerManager)
{
    Database& db = ledgerManager.getDatabase();


	switch (mManageAsset.action)
	{
	case MANAGE_ASSET_CREATE:
	{
		if (AssetFrame::exists(db, mManageAsset.code))
		{
			app.getMetrics().NewMeter({ "op-manage-asset", "invalid", "already-exists" },
				"operation").Mark();
			innerResult().code(MANAGE_ASSET_ALREADY_EXISTS);
			return false;
		}

		// todo check get
		auto asset = AssetFrame::create(mManageAsset.code, mManageAsset.policies);
		if (!createAsset(asset, app, ledgerManager, db, delta))
			return false;

		break;
	}
	case MANAGE_ASSET_UPDATE_POLICIES:
	{
		auto assetFrame = AssetFrame::loadAsset(mManageAsset.code, db, &delta);
		if (!assetFrame)
		{
			app.getMetrics().NewMeter({ "op-manage-asset", "invalid", "not-found" },
				"operation").Mark();
			innerResult().code(MANAGE_ASSET_NOT_FOUND);
			return false;
		}

		assetFrame->setPolicies(mManageAsset.policies);

		assetFrame->storeChange(delta, db);
		break;
	}
	default:
		throw std::runtime_error("Unexpected manage asset action");
	}    
    
	app.getMetrics().NewMeter({"op-manage-asset", "success", "apply"},
	                          "operation").Mark();
	innerResult().code(MANAGE_ASSET_SUCCESS);
	return true;
}

    bool
ManageAssetOpFrame::doCheckValid(Application& app)
{
    if (!isAssetValid(mManageAsset.code))
    {
        app.getMetrics().NewMeter({"op-manage-asset", "invalid",
                          "malformed-invalid-asset"},
                         "operation").Mark();
        innerResult().code(MANAGE_ASSET_MALFORMED);
        return false;
    }

	if (!isValidManageAssetAction(mManageAsset.action))
	{
		app.getMetrics().NewMeter({ "op-manage-asset", "invalid",
			"malformed-invalid-action" },
			"operation").Mark();
		innerResult().code(MANAGE_ASSET_MALFORMED);
		return false;
	}

    if(mManageAsset.policies < 0)
    {
        app.getMetrics().NewMeter({"op-manage-asset", "invalid",
                          "malformed-invalid-policies"},
                         "operation").Mark();
        innerResult().code(MANAGE_ASSET_MALFORMED);
        return false;
    }

    return true;
}
}
