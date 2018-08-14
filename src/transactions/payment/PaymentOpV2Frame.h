#pragma once

#include <transactions/OperationFrame.h>

namespace stellar {
    class PaymentOpV2Frame : public OperationFrame {
        PaymentV2Result &innerResult() {
            return mResult.tr().paymentV2Result();
        }

        PaymentOpV2 const &mPayment;

        std::unordered_map<AccountID, CounterpartyDetails>
        getCounterpartyDetails(Database &db, LedgerDelta *delta) const override;

        SourceDetails
        getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
                                int32_t ledgerVersion) const override;

        bool isRecipientFeeNotRequired();

        bool isDestinationFeeValid();

        BalanceFrame::pointer tryLoadDestinationBalance(AssetCode asset, Database &db, LedgerDelta &delta,
                                                        LedgerManager& lm);

        bool isTransferAllowed(BalanceFrame::pointer from, BalanceFrame::pointer to, Database &db);

        FeeDataV2 getActualFee(AccountFrame::pointer accountFrame, AssetCode const &transferAsset, uint64_t amount,
                               PaymentFeeType feeType, Database &db, LedgerManager& lm);

        bool processTransfer(AccountManager &accountManager, AccountFrame::pointer payer, BalanceFrame::pointer from, BalanceFrame::pointer to,
                             uint64_t amount, uint64_t& universalAmount, Database &db);

        bool processTransferFee(AccountManager &accountManager, AccountFrame::pointer payer,
                                BalanceFrame::pointer candidateToCharge, FeeDataV2 expectedFee, FeeDataV2 actualFee,
                                AccountID const &commissionID, Database &db, LedgerDelta &delta, bool ignoreStats,
                                uint64_t& universalAmount);

        void setErrorCode(AccountManager::Result transferResult);

        bool isSendToSelf(LedgerManager& lm, BalanceID sourceBalanceID, BalanceID destBalanceID);

    public:
        PaymentOpV2Frame(Operation const &op, OperationResult &res, TransactionFrame &parentTx);

        bool doApply(Application &app, StorageHelper& storageHelper, LedgerManager &ledgerManager) override;

        bool doCheckValid(Application &app) override;

        static PaymentV2ResultCode getInnerCode(OperationResult const &res) {
            return res.tr().paymentV2Result().code();
        }

        std::string getInnerResultCodeAsStr() override {
            return xdr::xdr_traits<PaymentV2ResultCode>::enum_name(innerResult().code());
        }
    };
}