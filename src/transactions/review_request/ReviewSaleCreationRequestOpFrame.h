#pragma once

// Copyright 2015 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ReviewRequestOpFrame.h"
#include "ledger/ReviewableRequestFrame.h"
#include "ledger/SaleFrame.h"

namespace stellar
{
class ReviewSaleCreationRequestOpFrame : public ReviewRequestOpFrame
{
public:

	  ReviewSaleCreationRequestOpFrame(Operation const& op, OperationResult& res,
                       TransactionFrame& parentTx);

          static uint64 getRequiredBaseAssetForHardCap(SaleCreationRequest const& saleCreationRequest);
protected:
    bool handleApprove(Application& app, LedgerDelta& delta, LedgerManager& ledgerManager, ReviewableRequestFrame::pointer request) override;

	SourceDetails getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
                                          int32_t ledgerVersion) const override;

    void createAssetPair(SaleFrame::pointer sale, Application &app, LedgerManager &ledgerManager, LedgerDelta &delta) const;

    std::map<AssetCode, BalanceID> loadBalances(AccountManager& accountManager, ReviewableRequestFrame::pointer request, SaleCreationRequest const& sale);
};
}
