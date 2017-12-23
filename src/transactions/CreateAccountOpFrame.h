#pragma once

// Copyright 2015 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "transactions/OperationFrame.h"

namespace stellar {
    class CreateAccountOpFrame : public OperationFrame {
    public:

        CreateAccountOpFrame(Operation const &op, OperationResult &res,
                             TransactionFrame &parentTx);

        bool doCheckValid(Application &app) override;
        bool doApply(Application &app, LedgerDelta &delta, LedgerManager &ledgerManager) override;
        bool tryUpdateAccountType(Application &app, LedgerDelta &delta, Database &db,
                                  AccountFrame::pointer &destAccountFrame);

        static CreateAccountResultCode getInnerCode(OperationResult const &res) {
            return res.tr().createAccountResult().code();
        }

    private:
        CreateAccountResult &innerResult() {
            return mResult.tr().createAccountResult();
        }

        void buildAccount(Application &app, LedgerDelta &delta, AccountFrame::pointer destAccountFrame);
        void trySetReferrer(Application &app, Database &db, AccountFrame::pointer destAccount) const;
        void storeExternalSystemsIDs(Application &app, LedgerDelta &delta,
                                     Database &db, const AccountFrame::pointer account);

        bool isAllowedToUpdateAccountType(AccountFrame::pointer destAccount) const;

        bool createAccount(Application &app, LedgerDelta &delta,
                           LedgerManager &ledgerManager);

        void createBalance(LedgerDelta& delta, Database &db);

        std::unordered_map<AccountID, CounterpartyDetails> getCounterpartyDetails(
                Database &db, LedgerDelta *delta) const override;

        SourceDetails getSourceAccountDetails(
                std::unordered_map<AccountID, CounterpartyDetails>
                counterpartiesDetails) const override;

        CreateAccountOp const &mCreateAccount;
    };
}
