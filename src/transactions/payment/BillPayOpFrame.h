#pragma once

#include <transactions/OperationFrame.h>

namespace stellar
{
class BillPayOpFrame : public OperationFrame {
    BillPayResult &innerResult()
    {
        return mResult.tr().billPayResult();
    }

    BillPayOp const& mBillPay;

    std::map<PaymentV2ResultCode, BillPayResultCode> paymentCodeToBillPayCode =
        {
            {PaymentV2ResultCode::MALFORMED, BillPayResultCode::MALFORMED},
            {PaymentV2ResultCode::UNDERFUNDED, BillPayResultCode::UNDERFUNDED},
            {PaymentV2ResultCode::LINE_FULL, BillPayResultCode::LINE_FULL},
            {PaymentV2ResultCode::FEE_ASSET_MISMATCHED, BillPayResultCode::FEE_ASSET_MISMATCHED},
            {PaymentV2ResultCode::DESTINATION_BALANCE_NOT_FOUND, BillPayResultCode::DESTINATION_BALANCE_NOT_FOUND},
            {PaymentV2ResultCode::INVALID_DESTINATION_FEE, BillPayResultCode::INVALID_DESTINATION_FEE},
            {PaymentV2ResultCode::BALANCE_ASSETS_MISMATCHED, BillPayResultCode::BALANCE_ASSETS_MISMATCHED},
            {PaymentV2ResultCode::SRC_BALANCE_NOT_FOUND, BillPayResultCode::SRC_BALANCE_NOT_FOUND},
            {PaymentV2ResultCode::REFERENCE_DUPLICATION, BillPayResultCode::REFERENCE_DUPLICATION},
            {PaymentV2ResultCode::STATS_OVERFLOW, BillPayResultCode::STATS_OVERFLOW},
            {PaymentV2ResultCode::LIMITS_EXCEEDED, BillPayResultCode::LIMITS_EXCEEDED},
            {PaymentV2ResultCode::NOT_ALLOWED_BY_ASSET_POLICY, BillPayResultCode::NOT_ALLOWED_BY_ASSET_POLICY},
            {PaymentV2ResultCode::INVALID_DESTINATION_FEE_ASSET, BillPayResultCode::INVALID_DESTINATION_FEE_ASSET},
            {PaymentV2ResultCode::INSUFFICIENT_FEE_AMOUNT, BillPayResultCode::INSUFFICIENT_FEE_AMOUNT},
            {PaymentV2ResultCode::BALANCE_TO_CHARGE_FEE_FROM_NOT_FOUND, BillPayResultCode::BALANCE_TO_CHARGE_FEE_FROM_NOT_FOUND},
            {PaymentV2ResultCode::PAYMENT_AMOUNT_IS_LESS_THAN_DEST_FEE, BillPayResultCode::PAYMENT_AMOUNT_IS_LESS_THAN_DEST_FEE},
        };

    std::unordered_map<AccountID, CounterpartyDetails>
    getCounterpartyDetails(Database &db, LedgerDelta *delta) const override;

    SourceDetails
    getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
                            int32_t ledgerVersion) const override;

    bool checkPaymentDetails(ReviewableRequestEntry& requestEntry,
                             BalanceID receiverBalance, BalanceID senderBalance);

    bool processPaymentV2(Application &app, LedgerDelta &delta, LedgerManager &ledgerManager);

    void trySetErrorCode(PaymentV2ResultCode paymentResult);

public:
    BillPayOpFrame(Operation const &op, OperationResult &res, TransactionFrame &parentTx);

    bool doApply(Application &app, LedgerDelta &delta, LedgerManager &ledgerManager) override;

    bool doCheckValid(Application &app) override;

    static BillPayResultCode getInnerCode(OperationResult const &res)
    {
        return res.tr().billPayResult().code();
    }

    std::string getInnerResultCodeAsStr() override
    {
        return xdr::xdr_traits<BillPayResultCode>::enum_name(innerResult().code());
    }
};
}