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

    std::unordered_map<AccountID, CounterpartyDetails>
    getCounterpartyDetails(Database &db, LedgerDelta *delta) const override;

    SourceDetails
    getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
                            int32_t ledgerVersion) const override;

    bool checkPaymentDetails(ReviewableRequestEntry& requestEntry);

    bool processPaymentV2(Application &app, LedgerDelta &delta, LedgerManager &ledgerManager);

    void setErrorCode(PaymentV2ResultCode paymentResult);

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