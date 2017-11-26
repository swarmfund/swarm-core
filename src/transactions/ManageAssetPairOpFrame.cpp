// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "transactions/ManageAssetPairOpFrame.h"
#include "transactions/ManageOfferOpFrame.h"
#include "ledger/LedgerDelta.h"
#include "ledger/AssetFrame.h"
#include "ledger/OfferFrame.h"

#include "database/Database.h"

#include "main/Application.h"
#include "medida/meter.h"
#include "medida/metrics_registry.h"

namespace stellar
{

using namespace std;
using xdr::operator==;
    
ManageAssetPairOpFrame::ManageAssetPairOpFrame(Operation const& op,
                                           OperationResult& res,
                                           TransactionFrame& parentTx)
    : OperationFrame(op, res, parentTx)
    , mManageAssetPair(mOperation.body.manageAssetPairOp())
{
}

std::unordered_map<AccountID, CounterpartyDetails> ManageAssetPairOpFrame::getCounterpartyDetails(Database & db, LedgerDelta * delta) const
{
	// no counterparties
	return std::unordered_map<AccountID, CounterpartyDetails>();
}

SourceDetails ManageAssetPairOpFrame::getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails) const
{
	int32_t signerType = mManageAssetPair.action == MANAGE_ASSET_PAIR_UPDATE_PRICE ? SIGNER_ASSET_RATE_MANAGER : SIGNER_ASSET_MANAGER;
	return SourceDetails({MASTER}, mSourceAccount->getHighThreshold(), signerType);
}

bool ManageAssetPairOpFrame::createNewAssetPair(Application& app, LedgerDelta& delta, LedgerManager& ledgerManager, AssetPairFrame::pointer assetPair)
{
	Database& db = ledgerManager.getDatabase();
	// already exists or reverced already exists
	if (assetPair || AssetPairFrame::exists(db, mManageAssetPair.quote, mManageAssetPair.base))
	{
		app.getMetrics().NewMeter({ "op-manage-asset-pair", "invalid", "already-exists" },
			"operation").Mark();
		innerResult().code(MANAGE_ASSET_PAIR_ALREADY_EXISTS);
		return false;
	}

	bool assetsExist = AssetFrame::exists(db, mManageAssetPair.base);
	assetsExist = assetsExist && AssetFrame::exists(db, mManageAssetPair.quote);
	if (!assetsExist)
	{
		app.getMetrics().NewMeter({ "op-manage-asset-pair", "invalid", "asset-not-exists" },
			"operation").Mark();
		innerResult().code(MANAGE_ASSET_PAIR_ASSET_NOT_FOUND);
		return false;
	}

	assetPair = AssetPairFrame::create(mManageAssetPair.base, mManageAssetPair.quote, mManageAssetPair.physicalPrice,
		mManageAssetPair.physicalPrice, mManageAssetPair.physicalPriceCorrection,
		mManageAssetPair.maxPriceStep, mManageAssetPair.policies);
	assetPair->storeAdd(delta, db);
	app.getMetrics().NewMeter({ "op-manage-asset-pair", "success", "apply" },
		"operation").Mark();
	innerResult().code(MANAGE_ASSET_PAIR_SUCCESS);
	innerResult().success().currentPrice = assetPair->getCurrentPrice();
	return true;
}

bool
ManageAssetPairOpFrame::doApply(Application& app,
                              LedgerDelta& delta, LedgerManager& ledgerManager)
{
	Database& db = ledgerManager.getDatabase();
	AssetPairFrame::pointer assetPair = AssetPairFrame::loadAssetPair(mManageAssetPair.base, mManageAssetPair.quote, db, &delta);
    if (mManageAssetPair.action == MANAGE_ASSET_PAIR_CREATE)
    {
		return createNewAssetPair(app, delta, ledgerManager, assetPair);
    }

	if (!assetPair)
	{
		app.getMetrics().NewMeter({ "op-manage-asset-pair", "invalid",
			"not-found" },
			"operation").Mark();
		innerResult().code(MANAGE_ASSET_PAIR_NOT_FOUND);
		return false;
	}
    
	auto& assetPairEntry = assetPair->getAssetPair();
	if (mManageAssetPair.action == MANAGE_ASSET_PAIR_UPDATE_POLICIES)
    {
		assetPairEntry.maxPriceStep = mManageAssetPair.maxPriceStep;
		assetPairEntry.physicalPriceCorrection = mManageAssetPair.physicalPriceCorrection;
		assetPairEntry.policies = mManageAssetPair.policies;
		// if pair not tradable remove all offers
		if (!assetPair->checkPolicy(ASSET_PAIR_TRADEABLE))
		{
			ManageOfferOpFrame::removeOffersBelowPrice(db, delta, assetPair, INT64_MAX);
		}
	}
	else 
	{
		int64_t premium = assetPairEntry.currentPrice - assetPairEntry.physicalPrice;
		if (premium < 0)
		{
			premium = 0;
		}
		assetPairEntry.physicalPrice = mManageAssetPair.physicalPrice;
		assetPairEntry.currentPrice = mManageAssetPair.physicalPrice + premium;
		ManageOfferOpFrame::removeOffersBelowPrice(db, delta, assetPair, assetPair->getMinAllowedPrice());
	}
   
	assetPair->storeChange(delta, db);
    
	app.getMetrics().NewMeter({"op-manage-asset-pair", "success", "apply"},
	                          "operation").Mark();
	innerResult().code(MANAGE_ASSET_PAIR_SUCCESS);
	innerResult().success().currentPrice = assetPair->getCurrentPrice();
	return true;
}



bool
ManageAssetPairOpFrame::doCheckValid(Application& app)
{
    if (!AssetFrame::isAssetCodeValid(mManageAssetPair.base) || !AssetFrame::isAssetCodeValid(mManageAssetPair.quote))
    {
        app.getMetrics().NewMeter({"op-manage-asset-pair", "invalid",
                          "malformed-invalid-asset-pair"},
                         "operation").Mark();
        innerResult().code(MANAGE_ASSET_PAIR_INVALID_ASSET);
        return false;
    }

	if (!isValidManageAssetPairAction(mManageAssetPair.action))
	{
		app.getMetrics().NewMeter({ "op-manage-asset-pair", "invalid",
			"malformed-invalid-action" },
			"operation").Mark();
		innerResult().code(MANAGE_ASSET_PAIR_INVALID_ACTION);
		return false;
	}
    
	if (mManageAssetPair.physicalPrice < 0 || (mManageAssetPair.action == MANAGE_ASSET_PAIR_UPDATE_PRICE && mManageAssetPair.physicalPrice == 0))
	{
		app.getMetrics().NewMeter({ "op-manage-asset-pair", "invalid",
			"malformed-invalid-physical-price" },
			"operation").Mark();
		innerResult().code(MANAGE_ASSET_PAIR_MALFORMED);
		return false;
	}

	if (mManageAssetPair.policies < 0)
	{
		app.getMetrics().NewMeter({ "op-manage-assetPair", "invalid",
			"malformed-invalid-policies" },
			"operation").Mark();
		innerResult().code(MANAGE_ASSET_PAIR_INVALID_POLICIES);
		return false;
	}


	if (mManageAssetPair.physicalPriceCorrection < 0)
	{
		app.getMetrics().NewMeter({ "op-manage-asset-pair", "invalid",
			"malformed-invalid-phusical-price-correction" },
			"operation").Mark();
		innerResult().code(MANAGE_ASSET_PAIR_MALFORMED);
		return false;
	}

	if (mManageAssetPair.maxPriceStep < 0 || mManageAssetPair.maxPriceStep > (100 * ONE))
	{
		app.getMetrics().NewMeter({ "op-manage-asset-pair", "invalid",
			"malformed-invalid-max-price-step" },
			"operation").Mark();
		innerResult().code(MANAGE_ASSET_PAIR_MALFORMED);
		return false;
	}


    return true;
}
}
