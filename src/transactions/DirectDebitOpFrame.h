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
            {PaymentResultCode::SUCCESS, DirectDebitResultCode::SUCCESS},
            {PaymentResultCode::MALFORMED, DirectDebitResultCode::MALFORMED},
            {PaymentResultCode::UNDERFUNDED, DirectDebitResultCode::UNDERFUNDED},
            {PaymentResultCode::LINE_FULL, DirectDebitResultCode::LINE_FULL},
            {PaymentResultCode::FEE_MISMATCHED, DirectDebitResultCode::FEE_MISMATCHED},
            {PaymentResultCode::BALANCE_NOT_FOUND, DirectDebitResultCode::BALANCE_NOT_FOUND},
            {PaymentResultCode::BALANCE_ACCOUNT_MISMATCHED, DirectDebitResultCode::BALANCE_ACCOUNT_MISMATCHED},
            {PaymentResultCode::BALANCE_ASSETS_MISMATCHED, DirectDebitResultCode::BALANCE_ASSETS_MISMATCHED},
            {PaymentResultCode::SRC_BALANCE_NOT_FOUND, DirectDebitResultCode::SRC_BALANCE_NOT_FOUND},
            {PaymentResultCode::REFERENCE_DUPLICATION, DirectDebitResultCode::REFERENCE_DUPLICATION},
            {PaymentResultCode::STATS_OVERFLOW, DirectDebitResultCode::STATS_OVERFLOW},
            {PaymentResultCode::LIMITS_EXCEEDED, DirectDebitResultCode::LIMITS_EXCEEDED},
            {PaymentResultCode::NOT_ALLOWED_BY_ASSET_POLICY, DirectDebitResultCode::NOT_ALLOWED_BY_ASSET_POLICY}
        };

	std::unordered_map<AccountID, CounterpartyDetails> getCounterpartyDetails(Database & db, LedgerDelta * delta) const override;
	SourceDetails getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
                                              int32_t ledgerVersion) const override;

  public:
    DirectDebitOpFrame(Operation const& op, OperationResult& res,
                   TransactionFrame& parentTx);

    bool doApply(Application& app, StorageHelper& storageHelper,
                 LedgerManager& ledgerManager) override;
    bool doCheckValid(Application& app) override;

    static DirectDebitResultCode
    getInnerCode(OperationResult const& res)
    {
        return res.tr().directDebitResult().code();
    }
};
}
