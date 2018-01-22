#pragma once

#include "transactions/OperationFrame.h"

namespace stellar {
    class PayoutOpFrame : public OperationFrame {

        PayoutResult &innerResult() {
            return mResult.tr().payoutResult();
        }

        PayoutOp const &mPayout;

        std::unordered_map<AccountID, CounterpartyDetails>
        getCounterpartyDetails(Database &db, LedgerDelta *delta) const override;

        SourceDetails getSourceAccountDetails(
                std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails) const override;

        bool isFeeMatches(AccountManager &accountManager, BalanceFrame::pointer balance) const;

    protected:

        uint64_t mActualPayoutAmount = 0;
        AssetFrame::pointer mAsset;
        std::vector<BalanceFrame::pointer> mHolders;
        std::vector<BalanceFrame::pointer> mReceivers;
        std::map<AccountID, uint64> mShareAmounts;
        BalanceFrame::pointer mSourceBalance;

    public:

        PayoutOpFrame(Operation const &op, OperationResult &res,
                      TransactionFrame &parentTx);

        bool doApply(Application &app, LedgerDelta &delta,
                     LedgerManager &ledgerManager) override;

        bool doCheckValid(Application &app) override;

        bool processBalanceChange(Application &app, AccountManager::Result balanceChangeResult);

        void addShareAmount(BalanceFrame::pointer const &holder);

        void addReceiver(AccountID const &shareholderID, Database &db, LedgerDelta &delta);

        static PayoutResultCode getInnerCode(OperationResult const &res) {
            return res.tr().payoutResult().code();
        }
    };
}