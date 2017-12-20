#pragma once

// Copyright 2015 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "transactions/OperationFrame.h"

namespace stellar
{
class CreateWithdrawalRequestOpFrame : public OperationFrame
{
    CreateWithdrawalRequestResult& innerResult()
    {
        return mResult.tr().createWithdrawalRequestResult();
    }

    CreateWithdrawalRequestOp const& mCreateWithdrawalRequest;

    std::unordered_map<AccountID, CounterpartyDetails> getCounterpartyDetails(
        Database& db, LedgerDelta* delta) const override;
    SourceDetails getSourceAccountDetails(
        std::unordered_map<AccountID, CounterpartyDetails>
        counterpartiesDetails) const override;

    BalanceFrame::pointer tryLoadBalance(Database& db, LedgerDelta& delta) const;

    bool isFeeMatches(AccountManager& accountManager, BalanceFrame::pointer balance) const;

    bool isConvertedAmountMatches(BalanceFrame::pointer balance, Database& db);

    bool tryLockBalance(BalanceFrame::pointer balance);

    bool tryAddStats(AccountManager& accountManager, BalanceFrame::pointer balance, uint64_t amountToAdd,
                         uint64_t& universalAmount);

public:

    CreateWithdrawalRequestOpFrame(Operation const& op, OperationResult& res,
                                   TransactionFrame& parentTx);
    bool doApply(Application& app, LedgerDelta& delta,
                 LedgerManager& ledgerManager) override;

    bool doCheckValid(Application& app) override;

    static CreateWithdrawalRequestResultCode getInnerCode(
        OperationResult const& res)
    {
        return res.tr().createWithdrawalRequestResult().code();
    }

    std::string getInnerResultCodeAsStr() override {
        return xdr::xdr_traits<CreateWithdrawalRequestResultCode>::enum_name(innerResult().code());
    }
};
}
