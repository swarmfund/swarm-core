#pragma once

// Copyright 2015 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "transactions/OperationFrame.h"


namespace stellar {
    class SetFeesOpFrame : public OperationFrame {
        SetFeesResult &
        innerResult() {
            return mResult.tr().setFeesResult();
        }

        SetFeesOp const &mSetFees;

        std::unordered_map<AccountID, CounterpartyDetails>
        getCounterpartyDetails(Database &db, LedgerDelta *delta) const override;

        SourceDetails getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
                                              int32_t ledgerVersion) const override;

        bool mustEmptyFixed(FeeEntry const &fee, medida::MetricsRegistry &metrics);

        bool mustEmptyPercent(FeeEntry const &fee, medida::MetricsRegistry &metrics);

        bool mustValidFeeAmounts(FeeEntry const &fee, medida::MetricsRegistry &media);

        bool mustFullRange(FeeEntry const &fee, medida::MetricsRegistry &media);

        bool mustDefaultSubtype(FeeEntry const &fee, medida::MetricsRegistry &metrics);

        bool mustBaseAsset(FeeEntry const &fee, Application &app);

        bool isPaymentFeeValid(FeeEntry const &fee, medida::MetricsRegistry &media);

        bool isOfferFeeValid(FeeEntry const &fee, medida::MetricsRegistry &media);

        bool isCapitalDeploymentFeeValid(FeeEntry const &fee, medida::MetricsRegistry &metrics);

        bool isForfeitFeeValid(FeeEntry const &fee, medida::MetricsRegistry &media);

        bool isEmissionFeeValid(FeeEntry const &fee, medida::MetricsRegistry &media);

        bool isInvestFeeValid(FeeEntry const &fee, medida::MetricsRegistry &metrics);

        bool isOperationFeeValid(FeeEntry const &fee, medida::MetricsRegistry &metrics);

        bool trySetFee(medida::MetricsRegistry &media, Database &db, LedgerDelta &delta);

        bool doCheckForfeitFee(medida::MetricsRegistry &media, Database &db, LedgerDelta &delta);

        bool doCheckPaymentFee(Database &db, LedgerDelta &delta);

    public:

        SetFeesOpFrame(Operation const &op, OperationResult &res,
                       TransactionFrame &parentTx);

        bool doApply(Application &app, LedgerDelta &delta,
                     LedgerManager &ledgerManager) override;

        bool doCheckValid(Application &app) override;

        static SetFeesResultCode
        getInnerCode(OperationResult const &res) {
            return res.tr().setFeesResult().code();
        }

        std::string getInnerResultCodeAsStr() override {
            return xdr::xdr_traits<SetFeesResultCode>::enum_name(innerResult().code());
        }
    };
}
