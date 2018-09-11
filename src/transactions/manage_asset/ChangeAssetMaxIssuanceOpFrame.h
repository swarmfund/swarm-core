#pragma once

// Copyright 2015 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ManageAssetOpFrame.h"

namespace stellar
{

class ChangeAssetMaxIssuanceOpFrame : public ManageAssetOpFrame
{

    SourceDetails getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
                                          int32_t ledgerVersion) const override;

    UpdateMaxIssuance mUpdateMaxIssuance;

public:
    ChangeAssetMaxIssuanceOpFrame(Operation const& op, OperationResult& res,
                         TransactionFrame& parentTx);

    bool doApply(Application& app, LedgerDelta& delta,
                 LedgerManager& ledgerManager) override;

    bool doCheckValid(Application& app) override;
protected:
    std::string getAssetCode() const override;
};
}
