#pragma once

// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ReviewRequestOpFrame.h"
#include "ledger/ReviewableRequestFrame.h"

namespace stellar
{
class ReviewLimitsUpdateRequestOpFrame : public ReviewRequestOpFrame
{
protected:
    bool handleApprove(Application& app, LedgerDelta& delta, LedgerManager& ledgerManager,
                       ReviewableRequestFrame::pointer request) override;

    bool handleReject(Application& app, LedgerDelta& delta, LedgerManager& ledgerManager,
                      ReviewableRequestFrame::pointer request) override;

    SourceDetails getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
                                          int32_t ledgerVersion) const override;

    bool tryCallManageLimits(Application &app, LedgerManager &ledgerManager, LedgerDelta &delta,
                             ReviewableRequestFrame::pointer request);

    bool handleManageLimitsResult(ManageLimitsResultCode manageLimitsResultCode);

public:
    ReviewLimitsUpdateRequestOpFrame(Operation const& op, OperationResult& res,
                                     TransactionFrame& parentTx);

};
}