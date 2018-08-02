#pragma once

// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ReviewRequestOpFrame.h"

namespace stellar
{
class ReviewInvoiceRequestOpFrame : public ReviewRequestOpFrame
{

    std::map<PaymentV2ResultCode, ReviewRequestResultCode> paymentCodeToReviewRequestCode =
    {
        {PaymentV2ResultCode::MALFORMED, ReviewRequestResultCode::PAYMENT_V2_MALFORMED},
        {PaymentV2ResultCode::UNDERFUNDED, ReviewRequestResultCode::UNDERFUNDED},
        {PaymentV2ResultCode::LINE_FULL, ReviewRequestResultCode::LINE_FULL},
        {PaymentV2ResultCode::FEE_ASSET_MISMATCHED, ReviewRequestResultCode::FEE_ASSET_MISMATCHED},
        {PaymentV2ResultCode::DESTINATION_BALANCE_NOT_FOUND, ReviewRequestResultCode::DESTINATION_BALANCE_NOT_FOUND},
        {PaymentV2ResultCode::INVALID_DESTINATION_FEE, ReviewRequestResultCode::INVALID_DESTINATION_FEE},
        {PaymentV2ResultCode::BALANCE_ASSETS_MISMATCHED, ReviewRequestResultCode::BALANCE_ASSETS_MISMATCHED},
        {PaymentV2ResultCode::SRC_BALANCE_NOT_FOUND, ReviewRequestResultCode::SRC_BALANCE_NOT_FOUND},
        {PaymentV2ResultCode::REFERENCE_DUPLICATION, ReviewRequestResultCode::REFERENCE_DUPLICATION},
        {PaymentV2ResultCode::STATS_OVERFLOW, ReviewRequestResultCode::STATS_OVERFLOW},
        {PaymentV2ResultCode::LIMITS_EXCEEDED, ReviewRequestResultCode::LIMITS_EXCEEDED},
        {PaymentV2ResultCode::NOT_ALLOWED_BY_ASSET_POLICY, ReviewRequestResultCode::NOT_ALLOWED_BY_ASSET_POLICY},
        {PaymentV2ResultCode::INVALID_DESTINATION_FEE_ASSET, ReviewRequestResultCode::INVALID_DESTINATION_FEE_ASSET},
        {PaymentV2ResultCode::INSUFFICIENT_FEE_AMOUNT, ReviewRequestResultCode::INSUFFICIENT_FEE_AMOUNT},
        {PaymentV2ResultCode::BALANCE_TO_CHARGE_FEE_FROM_NOT_FOUND, ReviewRequestResultCode::BALANCE_TO_CHARGE_FEE_FROM_NOT_FOUND},
        {PaymentV2ResultCode::PAYMENT_AMOUNT_IS_LESS_THAN_DEST_FEE, ReviewRequestResultCode::PAYMENT_AMOUNT_IS_LESS_THAN_DEST_FEE},
    };

    bool checkPaymentDetails(ReviewableRequestEntry& requestEntry,
                             BalanceID receiverBalance, BalanceID senderBalance);

    bool processPaymentV2(Application &app, LedgerDelta &delta, LedgerManager &ledgerManager);

    bool tryLockAmount(BalanceFrame::pointer balance, uint64_t amount);

    void trySetErrorCode(PaymentV2ResultCode paymentResult);

protected:
    bool handleApprove(Application& app, LedgerDelta& delta, LedgerManager& ledgerManager,
                       ReviewableRequestFrame::pointer request) override;

    bool handleReject(Application& app, LedgerDelta& delta, LedgerManager& ledgerManager,
                      ReviewableRequestFrame::pointer request) override;

    bool handlePermanentReject(Application& app, LedgerDelta& delta, LedgerManager& ledgerManager,
                      ReviewableRequestFrame::pointer request) override;

    SourceDetails getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
                                          int32_t ledgerVersion) const override;

public:
    ReviewInvoiceRequestOpFrame(Operation const& op, OperationResult& res,
                                     TransactionFrame& parentTx);

};
}