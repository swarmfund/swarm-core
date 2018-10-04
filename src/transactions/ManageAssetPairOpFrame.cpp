// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "transactions/ManageAssetPairOpFrame.h"
#include "transactions/dex/ManageOfferOpFrame.h"
#include "ledger/LedgerDelta.h"
#include "ledger/AssetFrame.h"
#include "ledger/AssetHelperLegacy.h"
#include "ledger/AssetPairHelper.h"

#include "database/Database.h"

#include "main/Application.h"
#include "medida/meter.h"
#include "medida/metrics_registry.h"
#include "ledger/OfferHelper.h"
#include "dex/OfferManager.h"
#include "ledger/BalanceHelperLegacy.h"

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

SourceDetails ManageAssetPairOpFrame::getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
                                                              int32_t ledgerVersion) const
{
	int32_t signerType = mManageAssetPair.action == ManageAssetPairAction::UPDATE_PRICE ?
						 							static_cast<int32_t >(SignerType::ASSET_RATE_MANAGER):
                                                    static_cast<int32_t >(SignerType::ASSET_MANAGER);
	return SourceDetails({AccountType::MASTER}, mSourceAccount->getHighThreshold(), signerType);
}

bool ManageAssetPairOpFrame::createNewAssetPair(Application& app, LedgerDelta& delta, LedgerManager& ledgerManager, AssetPairFrame::pointer assetPair)
{
	Database& db = ledgerManager.getDatabase();
	// already exists or reverced already exists
	
	auto assetPairHelper = AssetPairHelper::Instance();
	if (assetPair || assetPairHelper->exists(db, mManageAssetPair.quote, mManageAssetPair.base))
	{
		app.getMetrics().NewMeter({ "op-manage-asset-pair", "invalid", "already-exists" },
			"operation").Mark();
		innerResult().code(ManageAssetPairResultCode::ALREADY_EXISTS);
		return false;
	}

	auto assetHelper = AssetHelperLegacy::Instance();
	bool assetsExist = assetHelper->exists(db, mManageAssetPair.base);
	assetsExist = assetsExist && assetHelper->exists(db, mManageAssetPair.quote);
	if (!assetsExist)
	{
		app.getMetrics().NewMeter({ "op-manage-asset-pair", "invalid", "asset-not-exists" },
			"operation").Mark();
		innerResult().code(ManageAssetPairResultCode::ASSET_NOT_FOUND);
		return false;
	}

	assetPair = AssetPairFrame::create(mManageAssetPair.base, mManageAssetPair.quote, mManageAssetPair.physicalPrice,
		mManageAssetPair.physicalPrice, mManageAssetPair.physicalPriceCorrection,
		mManageAssetPair.maxPriceStep, mManageAssetPair.policies);
	EntryHelperProvider::storeAddEntry(delta, db, assetPair->mEntry);
        AccountManager::loadOrCreateBalanceForAsset(app.getCommissionID(), assetPair->getQuoteAsset(), db, delta);

	app.getMetrics().NewMeter({ "op-manage-asset-pair", "success", "apply" },
		"operation").Mark();
	innerResult().code(ManageAssetPairResultCode::SUCCESS);
	innerResult().success().currentPrice = assetPair->getCurrentPrice();
	return true;
}

bool
ManageAssetPairOpFrame::doApply(Application& app,
                              LedgerDelta& delta, LedgerManager& ledgerManager)
{
	Database& db = ledgerManager.getDatabase();

	auto assetPairHelper = AssetPairHelper::Instance();
	AssetPairFrame::pointer assetPair = assetPairHelper->loadAssetPair(mManageAssetPair.base, mManageAssetPair.quote, db, &delta);
    if (mManageAssetPair.action == ManageAssetPairAction::CREATE)
    {
		return createNewAssetPair(app, delta, ledgerManager, assetPair);
    }

	if (!assetPair)
	{
		app.getMetrics().NewMeter({ "op-manage-asset-pair", "invalid",
			"not-found" },
			"operation").Mark();
		innerResult().code(ManageAssetPairResultCode::NOT_FOUND);
		return false;
	}

	auto& assetPairEntry = assetPair->getAssetPair();
	if (mManageAssetPair.action == ManageAssetPairAction::UPDATE_POLICIES)
    {
		assetPairEntry.maxPriceStep = mManageAssetPair.maxPriceStep;
		assetPairEntry.physicalPriceCorrection = mManageAssetPair.physicalPriceCorrection;
		assetPairEntry.policies = mManageAssetPair.policies;
		// if pair not tradable remove all offers
		if (!assetPair->checkPolicy(AssetPairPolicy::TRADEABLE_SECONDARY_MARKET))
		{
		    auto orderBookID = ManageOfferOpFrame::SECONDARY_MARKET_ORDER_BOOK_ID;
		    const auto offersToRemove = OfferHelper::Instance()->loadOffersWithFilters(assetPair->getBaseAsset(), assetPair->getQuoteAsset(), &orderBookID, nullptr, db);
                    OfferManager::deleteOffers(offersToRemove, db, delta);
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
                auto orderBookID = ManageOfferOpFrame::SECONDARY_MARKET_ORDER_BOOK_ID;
                uint64_t minAllowedPrice = assetPair->getMinAllowedPrice();
                const auto offersToRemove = OfferHelper::Instance()->loadOffersWithFilters(assetPair->getBaseAsset(), assetPair->getQuoteAsset(), &orderBookID, &minAllowedPrice, db);
                OfferManager::deleteOffers(offersToRemove, db, delta);
	}

	EntryHelperProvider::storeChangeEntry(delta, db, assetPair->mEntry);

	app.getMetrics().NewMeter({"op-manage-asset-pair", "success", "apply"},
	                          "operation").Mark();
	innerResult().code(ManageAssetPairResultCode::SUCCESS);
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
        innerResult().code(ManageAssetPairResultCode::INVALID_ASSET);
        return false;
    }

	if (!isValidManageAssetPairAction(mManageAssetPair.action))
	{
		app.getMetrics().NewMeter({ "op-manage-asset-pair", "invalid",
			"malformed-invalid-action" },
			"operation").Mark();
		innerResult().code(ManageAssetPairResultCode::INVALID_ACTION);
		return false;
	}

	if (mManageAssetPair.physicalPrice < 0 ||
            (mManageAssetPair.action == ManageAssetPairAction::UPDATE_PRICE && mManageAssetPair.physicalPrice == 0))
	{
		app.getMetrics().NewMeter({ "op-manage-asset-pair", "invalid",
			"malformed-invalid-physical-price" },
			"operation").Mark();
		innerResult().code(ManageAssetPairResultCode::MALFORMED);
		return false;
	}

	if (mManageAssetPair.policies < 0)
	{
		app.getMetrics().NewMeter({ "op-manage-assetPair", "invalid",
			"malformed-invalid-policies" },
			"operation").Mark();
		innerResult().code(ManageAssetPairResultCode::INVALID_POLICIES);
		return false;
	}


	if (mManageAssetPair.physicalPriceCorrection < 0)
	{
		app.getMetrics().NewMeter({ "op-manage-asset-pair", "invalid",
			"malformed-invalid-phusical-price-correction" },
			"operation").Mark();
		innerResult().code(ManageAssetPairResultCode::MALFORMED);
		return false;
	}

	if (mManageAssetPair.maxPriceStep < 0 || mManageAssetPair.maxPriceStep > 100 * ONE)
	{
		app.getMetrics().NewMeter({ "op-manage-asset-pair", "invalid",
			"malformed-invalid-max-price-step" },
			"operation").Mark();
		innerResult().code(ManageAssetPairResultCode::MALFORMED);
		return false;
	}


    return true;
}
}
