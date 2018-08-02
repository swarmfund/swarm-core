#pragma once

// Copyright 2015 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "transactions/OperationFrame.h"
#include "ledger/ReviewableRequestFrame.h"

namespace stellar
{
class AddContractDetailsOpFrame : public OperationFrame
{
    std::unordered_map<AccountID, CounterpartyDetails> getCounterpartyDetails(Database& db,
                                                                              LedgerDelta* delta) const override;
    SourceDetails getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
                                          int32_t ledgerVersion) const override;

    AddContractDetailsResult&
    innerResult()
    {
        return mResult.tr().addContractDetailsResult();
    }

    AddContractDetailsOp const& mAddContractDetails;

public:
    AddContractDetailsOpFrame(Operation const& op, OperationResult& res,
                              TransactionFrame& parentTx);

    bool doApply(Application& app, LedgerDelta& delta,
                 LedgerManager& ledgerManager) override;
    bool doCheckValid(Application& app) override;

    static AddContractDetailsResultCode
    getInnerCode(OperationResult const& res)
    {
        return res.tr().addContractDetailsResult().code();
    }

    std::string getInnerResultCodeAsStr() override;
};
}
