#pragma once

// Copyright 2015 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "transactions/OperationFrame.h"
#include "ledger/ReviewableRequestFrame.h"

namespace stellar
{
class CreateSaleCreationRequestOpFrame : public OperationFrame
{
    CreateSaleCreationRequestResult& innerResult()
    {
        return mResult.tr().createSaleCreationRequestResult();
    }

    CreateSaleCreationRequestOp const& mCreateSaleCreationRequest;

    std::unordered_map<AccountID, CounterpartyDetails> getCounterpartyDetails(
        Database& db, LedgerDelta* delta) const override;
    SourceDetails getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
                                              int32_t ledgerVersion) const override;

    // tryLoadAssetOrRequest - tries to load base asset or request. If fails returns nullptr. If request exists - creates asset frame wrapper for it
    static AssetFrame::pointer tryLoadBaseAssetOrRequest(SaleCreationRequest const& request, Database& db, AccountID const& source);

    std::string getReference(SaleCreationRequest const& request) const;

    ReviewableRequestFrame::pointer createNewUpdateRequest(Application& app, LedgerManager& lm, Database& db, LedgerDelta& delta, time_t closedAt) const;

    // isBaseAssetHasSufficientIssuance - returns true, if base asset amount required for hard cap and soft cap does not exceed available amount to be issued.
    // sets corresponding result code
    bool isBaseAssetHasSufficientIssuance(AssetFrame::pointer assetFrame);
    static bool isPriceValid(SaleCreationRequestQuoteAsset const& quoteAsset,
                             SaleCreationRequest const& saleCreationRequest);
public:

    CreateSaleCreationRequestOpFrame(Operation const& op, OperationResult& res,
                                   TransactionFrame& parentTx);
    bool doApply(Application& app, LedgerDelta& delta,
                 LedgerManager& ledgerManager) override;

    bool doCheckValid(Application& app) override;

    static bool ensureEnoughAvailable(Application& app, const SaleCreationRequest& saleCreationRequest,
            const AssetFrame::pointer baseAsset);

    static CreateSaleCreationRequestResultCode doCheckValid(Application& app,
                                                            SaleCreationRequest const& saleCreationRequest, AccountID const& source);

    static bool areQuoteAssetsValid(Database& db, xdr::xvector<SaleCreationRequestQuoteAsset, 100> quoteAssets, AssetCode defaultQuoteAsset);

    static CreateSaleCreationRequestResultCode getInnerCode(
        OperationResult const& res)
    {
        return res.tr().createSaleCreationRequestResult().code();
    }

    std::string getInnerResultCodeAsStr() override {
        return xdr::xdr_traits<CreateSaleCreationRequestResultCode>::enum_name(innerResult().code());
    }
};
}
