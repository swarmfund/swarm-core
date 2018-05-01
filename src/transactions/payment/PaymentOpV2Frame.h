#pragma once

#include <transactions/OperationFrame.h>

namespace stellar {
    class PaymentOpV2Frame : public OperationFrame {
        PaymentV2Result &innerResult() {
            return mResult.tr().paymentV2Result();
        }

        PaymentOpV2 const &mPayment;

        BalanceFrame::pointer mSourceBalance;
        BalanceFrame::pointer mDestinationBalance;
        BalanceFrame::pointer mSourceFeeBalance;

        uint64_t mSourceSentUniversal;
        uint64_t mActualSourcePaymentFee;
        uint64_t mActualDestinationPaymentFee;

        bool mIsCrossAssetPayment;

        std::unordered_map<AccountID, CounterpartyDetails>
        getCounterpartyDetails(Database &db, LedgerDelta *delta) const override;

        SourceDetails
        getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
                                int32_t ledgerVersion) const override;

        bool isRecipientFeeNotRequired();

        bool tryLoadDestinationBalance(Database &db, LedgerDelta &delta);

        bool processBalanceChange(AccountManager::Result balanceChangeResult);

        bool isAllowedToTransfer(Database &db);

        bool processSourceFee(AccountManager &accountManager, Database &db, LedgerDelta &delta);

        bool processDestinationFee(AccountManager &accountManager, Database &db, LedgerDelta &delta);

        bool
        isTransferFeeMatch(AccountFrame::pointer accountFrame, AssetCode const &assetCode, FeeDataV2 const &feeData,
                           int64_t const &amount, int64_t subtype, Database &db, LedgerDelta &delta);

        bool tryLoadSourceFeeBalance(Database &db, LedgerDelta &delta);

        bool tryFundCommissionAccount(Application &app, Database &db, LedgerDelta &delta);

    public:
        PaymentOpV2Frame(Operation const &op, OperationResult &res, TransactionFrame &parentTx);

        bool doApply(Application &app, LedgerDelta &delta, LedgerManager &ledgerManager) override;

        bool doCheckValid(Application &app) override;

        static PaymentV2ResultCode getInnerCode(OperationResult const &res) {
            return res.tr().paymentV2Result().code();
        }

        std::string getInnerResultCodeAsStr() override {
            return xdr::xdr_traits<PaymentV2ResultCode>::enum_name(innerResult().code());
        }
    };
}