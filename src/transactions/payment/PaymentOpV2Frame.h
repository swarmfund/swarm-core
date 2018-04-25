#pragma once

#include <transactions/OperationFrame.h>

namespace stellar {
    class PaymentOpV2Frame : public OperationFrame {
        PaymentV2Result &innerResult() {
            return mResult.tr().paymentV2Result();
        }

        PaymentOpV2 const &mPaymentV2;
        uint64_t mSourceSent;
        uint64_t mDestReceived;

        std::unordered_map<AccountID, CounterpartyDetails>
        getCounterpartyDetails(Database &db, LedgerDelta *delta) const override;

        SourceDetails
        getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
                                int32_t ledgerVersion) const override;

        bool isRecipientFeeNotRequired(Database &db);

        bool isAllowedToTransfer(Database &db, AssetFrame::pointer asset);

        bool processFees(Application &app, LedgerDelta &delta, Database &db);

    protected:

        AccountFrame::pointer mDestAccount;
        BalanceFrame::pointer mSourceBalance;
        BalanceFrame::pointer mDestBalance;

        bool tryLoadBalances(Application &app, Database &db, LedgerDelta &delta);

        bool checkFees(Application &app, Database &db, LedgerDelta &delta);

    public:
        PaymentOpV2Frame(Operation const &op, OperationResult &res, TransactionFrame &parentTx);

        bool doApply(Application &app, LedgerDelta &delta, LedgerManager &ledgerManager) override;

        bool doCheckValid(Application &app) override;

        bool processBalanceChange(Application &app, AccountManager::Result balanceChangeResult);

        static bool isTransferFeeMatch(AccountFrame::pointer accountFrame, AssetCode const &assetCode,
                                       FeeDataV2 const &feeData, uint64_t const &amount, PaymentFeeType paymentFeeType,
                                       Database &db, LedgerDelta &delta);

        static PaymentV2ResultCode getInnerCode(OperationResult const &res) {
            return res.tr().paymentV2Result().code();
        }

        std::string getInnerResultCodeAsStr() override {
            return xdr::xdr_traits<PaymentV2ResultCode>::enum_name(innerResult().code());
        }
    };
}