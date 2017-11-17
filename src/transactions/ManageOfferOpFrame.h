#pragma once

// Copyright 2015 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ledger/OfferFrame.h"
#include "ledger/BalanceFrame.h"
#include "ledger/AssetPairFrame.h"
#include "transactions/OperationFrame.h"

namespace stellar
{
class ManageOfferOpFrame : public OperationFrame
{
    BalanceFrame::pointer mBaseBalance;
    BalanceFrame::pointer mQuoteBalance;
	AssetPairFrame::pointer mAssetPair;

    bool checkOfferValid(Application& app, LedgerManager& lm, Database& db,
                         LedgerDelta& delta);

	BalanceFrame::pointer loadBalanceValidForTrading(BalanceID const& balanceID, medida::MetricsRegistry& metrics, Database& db, LedgerDelta & delta);

	AssetPairFrame::pointer loadTradableAssetPair(medida::MetricsRegistry& metrics, Database& db, LedgerDelta & delta);

	// returns true if offer price does not violates physical price restriction
	bool checkPhysicalPriceRestrictionMet(AssetPairFrame::pointer assetPair, medida::MetricsRegistry& metrics);

	// returns true if offer price does not violates current price restriction
	bool checkCurrentPriceRestrictionMet(AssetPairFrame::pointer assetPair, medida::MetricsRegistry& metrics);

	bool deleteOffer(medida::MetricsRegistry& metrics, Database& db, LedgerDelta & delta);

	bool lockSellingAmount(OfferEntry const& offer);

	bool setFeeToBeCharged(OfferEntry& offer, AssetCode const& quoteAsset, Database& db);

    ManageOfferResult&
    innerResult()
    {
        return mResult.tr().manageOfferResult();
    }

    ManageOfferOp const& mManageOffer;

    OfferFrame::pointer buildOffer(ManageOfferOp const& op, AssetCode const& selling, AssetCode const& buying);

	int64_t getQuoteAmount();

	std::unordered_map<AccountID, CounterpartyDetails> getCounterpartyDetails(Database& db, LedgerDelta* delta) const override;
	SourceDetails getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails) const override;

  public:
    ManageOfferOpFrame(Operation const& op, OperationResult& res,
                       TransactionFrame& parentTx);

    bool doApply(Application& app, LedgerDelta& delta,
                 LedgerManager& ledgerManager) override;
    bool doCheckValid(Application& app) override;

	static void deleteOffer(OfferFrame::pointer offer, Database&db, LedgerDelta& delta);

	static void removeOffersBelowPrice(Database& db, LedgerDelta& delta, AssetPairFrame::pointer assetPair, int64_t price);

    static ManageOfferResultCode
    getInnerCode(OperationResult const& res)
    {
        return res.tr().manageOfferResult().code();
    }
};
}
