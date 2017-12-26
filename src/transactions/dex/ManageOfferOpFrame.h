#pragma once

// Copyright 2015 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ledger/OfferFrame.h"
#include "ledger/BalanceFrame.h"
#include "ledger/AssetPairFrame.h"
#include "transactions/OperationFrame.h"
#include "OfferExchange.h"

namespace stellar
{
class ManageOfferOpFrame : public OperationFrame
{
    BalanceFrame::pointer mBaseBalance;
    BalanceFrame::pointer mQuoteBalance;
    AssetPairFrame::pointer mAssetPair;

    bool checkOfferValid(Database& db, LedgerDelta& delta);

    OfferExchange::OfferFilterResult filterOffer(uint64_t price, OfferFrame const& o);

    BalanceFrame::pointer loadBalanceValidForTrading(
        BalanceID const& balanceID,
        Database& db, LedgerDelta& delta);

    AssetPairFrame::pointer loadTradableAssetPair(Database& db, LedgerDelta& delta);

    bool deleteOffer(Database& db, LedgerDelta& delta);

    bool lockSellingAmount(OfferEntry const& offer);

    bool setFeeToBeCharged(OfferEntry& offer, AssetCode const& quoteAsset,
                           Database& db);

    ManageOfferResult& innerResult()
    {
        return mResult.tr().manageOfferResult();
    }

    ManageOfferOp const& mManageOffer;

    OfferFrame::pointer buildOffer(ManageOfferOp const& op,
                                   AssetCode const& selling,
                                   AssetCode const& buying);

    int64_t getQuoteAmount();

    std::unordered_map<AccountID, CounterpartyDetails> getCounterpartyDetails(
        Database& db, LedgerDelta* delta) const override;
    SourceDetails getSourceAccountDetails(
        std::unordered_map<AccountID, CounterpartyDetails>
        counterpartiesDetails) const override;

public:
    static const uint64_t SECONDARY_MARKET_ORDER_BOOK_ID = 0;
    ManageOfferOpFrame(Operation const& op, OperationResult& res,
                       TransactionFrame& parentTx);

    bool doApply(Application& app, LedgerDelta& delta,
                 LedgerManager& ledgerManager) override;
    bool doCheckValid(Application& app) override;

    static void deleteOffer(OfferFrame::pointer offer, Database& db,
                            LedgerDelta& delta);

	static void removeOffersBelowPrice(Database& db, LedgerDelta& delta, AssetPairFrame::pointer assetPair, uint64_t* orderBookID, int64_t price);

    static ManageOfferResultCode getInnerCode(OperationResult const& res)
    {
        return res.tr().manageOfferResult().code();
    }

    static ManageOfferOpFrame* make(Operation const& op, OperationResult& res,
        TransactionFrame& parentTx);
    std::string getInnerResultCodeAsStr() override;
};
}
