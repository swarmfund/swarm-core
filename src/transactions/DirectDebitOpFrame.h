#pragma once

// Copyright 2015 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "transactions/OperationFrame.h"

namespace stellar
{

class DirectDebitOpFrame : public OperationFrame
{
    DirectDebitResult&
    innerResult()
    {
        return mResult.tr().directDebitResult();
    }
    DirectDebitOp const& mDirectDebit;

    std::map<PaymentResultCode, DirectDebitResultCode> paymentCodeToDebitCode = 
        {
            {PAYMENT_SUCCESS, DIRECT_DEBIT_SUCCESS},
            {PAYMENT_MALFORMED, DIRECT_DEBIT_MALFORMED},
            {PAYMENT_UNDERFUNDED, DIRECT_DEBIT_UNDERFUNDED},
            {PAYMENT_LINE_FULL, DIRECT_DEBIT_LINE_FULL},
            {PAYMENT_FEE_MISMATCHED, DIRECT_DEBIT_FEE_MISMATCHED},
            {PAYMENT_BALANCE_NOT_FOUND, DIRECT_DEBIT_BALANCE_NOT_FOUND},
            {PAYMENT_BALANCE_ACCOUNT_MISMATCHED, DIRECT_DEBIT_BALANCE_ACCOUNT_MISMATCHED},
            {PAYMENT_BALANCE_ASSETS_MISMATCHED, DIRECT_DEBIT_BALANCE_ASSETS_MISMATCHED},
            {PAYMENT_SRC_BALANCE_NOT_FOUND, DIRECT_DEBIT_SRC_BALANCE_NOT_FOUND},
            {PAYMENT_REFERENCE_DUPLICATION, DIRECT_DEBIT_REFERENCE_DUPLICATION},
            {PAYMENT_STATS_OVERFLOW, DIRECT_DEBIT_STATS_OVERFLOW},
            {PAYMENT_LIMITS_EXCEEDED, DIRECT_DEBIT_LIMITS_EXCEEDED},
            {PAYMENT_NOT_ALLOWED_BY_ASSET_POLICY, DIRECT_DEBIT_NOT_ALLOWED_BY_ASSET_POLICY}
        };

	std::unordered_map<AccountID, CounterpartyDetails> getCounterpartyDetails(Database & db, LedgerDelta * delta) const override;
	SourceDetails getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails) const override;

  public:
    DirectDebitOpFrame(Operation const& op, OperationResult& res,
                   TransactionFrame& parentTx);

    bool doApply(Application& app, LedgerDelta& delta,
                 LedgerManager& ledgerManager) override;
    bool doCheckValid(Application& app) override;

    static DirectDebitResultCode
    getInnerCode(OperationResult const& res)
    {
        return res.tr().directDebitResult().code();
    }
};
}
