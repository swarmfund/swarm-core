#pragma once

// Copyright 2015 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "transactions/OperationFrame.h"


namespace stellar
{
    class SetFeesOpFrame : public OperationFrame
    {
        SetFeesResult&
        innerResult()
        {
            return mResult.tr().setFeesResult();
        }
        
        SetFeesOp const& mSetFees;
        
		std::unordered_map<AccountID, CounterpartyDetails> getCounterpartyDetails(Database& db, LedgerDelta* delta) const override;
		SourceDetails getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails) const override;

		bool mustEmptyFixed(FeeEntry const& fee, medida::MetricsRegistry& metrics);
		bool mustValidFeeAmounts(FeeEntry const& fee, medida::MetricsRegistry& metrics);
		bool mustFullRange(FeeEntry const& fee, medida::MetricsRegistry& metrics);
		bool mustDefaultSubtype(FeeEntry const& fee, medida::MetricsRegistry& metrics);
		bool mustBaseAsset(FeeEntry const& fee, Application& app);

		bool isPaymentFeeValid(FeeEntry const& fee, medida::MetricsRegistry& metrics);
		bool isOfferFeeValid(FeeEntry const& fee, medida::MetricsRegistry& metrics);
        bool isForfeitFeeValid(FeeEntry const& fee, medida::MetricsRegistry& metrics);
        bool isEmissionFeeValid(FeeEntry const& fee, medida::MetricsRegistry& metrics);
		bool isPayoutFeeValid(FeeEntry const& fee, medida::MetricsRegistry& metrics);

		bool trySetFee(medida::MetricsRegistry& metrics, Database& db, LedgerDelta& delta);

		bool doCheckForfeitFee(medida::MetricsRegistry& metrics, Database& db, LedgerDelta& delta);

    public:

        SetFeesOpFrame(Operation const& op, OperationResult& res,
                                          TransactionFrame& parentTx);
        
        bool doApply(Application& app, LedgerDelta& delta,
                     LedgerManager& ledgerManager) override;
        bool doCheckValid(Application& app) override;
        
        static SetFeesResultCode
        getInnerCode(OperationResult const& res)
        {
            return res.tr().setFeesResult().code();
        }
    };
}
