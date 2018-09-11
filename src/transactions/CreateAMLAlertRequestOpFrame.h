//
// Created by Roman on 02.03.18.
//

#pragma once

// Copyright 2015 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "transactions/OperationFrame.h"
#include "ledger/ReviewableRequestFrame.h"

namespace stellar {
    class CreateAMLAlertRequestOpFrame : public OperationFrame {
        CreateAMLAlertRequestResult &innerResult() {
            return mResult.tr().createAMLAlertRequestResult();
        }

        CreateAMLAlertRequestOp const &mCreateAMLAlertRequest;

        std::unordered_map<AccountID, CounterpartyDetails> getCounterpartyDetails(
                Database &db, LedgerDelta *delta) const override;

        SourceDetails getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
                                              int32_t ledgerVersion) const override;


    public:

        CreateAMLAlertRequestOpFrame(Operation const &op, OperationResult &res,
                                     TransactionFrame &parentTx);

        bool doApply(Application &app, LedgerDelta &delta,
                     LedgerManager &ledgerManager) override;

        bool doCheckValid(Application &app) override;

        static CreateAMLAlertRequestResultCode getInnerCode(
                OperationResult const &res) {
            return res.tr().createAMLAlertRequestResult().code();
        }

        std::string getInnerResultCodeAsStr() override {
            return xdr::xdr_traits<CreateAMLAlertRequestResultCode>::enum_name(innerResult().code());
        }
    };
}
