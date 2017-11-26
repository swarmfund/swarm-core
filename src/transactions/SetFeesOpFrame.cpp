// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "transactions/SetFeesOpFrame.h"
#include "transactions/ManageOfferOpFrame.h"
#include "ledger/LedgerDelta.h"
#include "ledger/FeeFrame.h"
#include "ledger/AssetPairFrame.h"
#include "database/Database.h"

#include "main/Application.h"
#include "medida/meter.h"
#include "medida/metrics_registry.h"

namespace stellar
{
    
    using namespace std;
    using xdr::operator==;
    
    SetFeesOpFrame::SetFeesOpFrame(Operation const& op,
                                               OperationResult& res,
                                               TransactionFrame& parentTx)
    : OperationFrame(op, res, parentTx)
    , mSetFees(mOperation.body.setFeesOp())
    {
    }

	bool SetFeesOpFrame::trySetFee(medida::MetricsRegistry& metrics, Database& db, LedgerDelta& delta)
	{
		assert(mSetFees.fee);
		if (mSetFees.fee->feeType == FeeType::FORFEIT_FEE && !doCheckForfeitFee(metrics, db, delta))
			return false;


		Hash hash = FeeFrame::calcHash(mSetFees.fee->feeType, mSetFees.fee->asset, mSetFees.fee->accountID.get(), mSetFees.fee->accountType.get(), mSetFees.fee->subtype);
		auto feeFrame = FeeFrame::loadFee(hash, mSetFees.fee->lowerBound, mSetFees.fee->upperBound, db, &delta);
		// delete
		if (mSetFees.isDelete)
		{
			if (!feeFrame)
			{
				innerResult().code(SET_FEES_NOT_FOUND);
				metrics.NewMeter({ "op-set-fees", "invalid", "fee-not-found" }, "operation").Mark();
				return false;
			}

			feeFrame->storeDelete(delta, db);
			return true;
		}

		// update
		if (feeFrame)
		{
			auto& fee = feeFrame->getFee();
			fee.percentFee = mSetFees.fee->percentFee;
			fee.fixedFee = mSetFees.fee->fixedFee;
			feeFrame->storeChange(delta, db);
			return true;
		}

		// create 
		if (!AssetFrame::exists(db, mSetFees.fee->asset))
		{
			innerResult().code(SET_FEES_ASSET_NOT_FOUND);
			metrics.NewMeter({ "op-set-fees", "invalid", "asset-not-found" }, "operation").Mark();
			return false;
		}

		// for fererral fee and storage fee it is only allowed  to have full range, so we can update them without check for range overlap
		if (mSetFees.fee->feeType != REFERRAL_FEE)
		{
			if (FeeFrame::isBoundariesOverlap(hash, mSetFees.fee->lowerBound, mSetFees.fee->upperBound, db))
			{
				innerResult().code(SET_FEES_RANGE_OVERLAP);
				metrics.NewMeter({ "op-set-fees", "invalid", "boundaries-overlap" }, "operation").Mark();
				return false;
			}
		}

		LedgerEntry le;
		le.data.type(FEE);
		le.data.feeState() = *mSetFees.fee;
		feeFrame = make_shared<FeeFrame>(le);
		feeFrame->storeAdd(delta, db);
		return true;
	}

	bool SetFeesOpFrame::doCheckForfeitFee(medida::MetricsRegistry & metrics, Database & db, LedgerDelta & delta)
	{
		auto asset = AssetFrame::loadAsset(mSetFees.fee->asset, db);
		if (!asset)
		{
			innerResult().code(SET_FEES_ASSET_NOT_FOUND);
			metrics.NewMeter({ "op-set-fees", "invalid", "asset-not-exist" }, "operation").Mark();
			return false;
		}

		return true;
	}

    bool
    SetFeesOpFrame::doApply(Application& app,
                                  LedgerDelta& d, LedgerManager& ledgerManager)
    {
        Database& db = ledgerManager.getDatabase();
        innerResult().code(SET_FEES_SUCCESS);

		LedgerDelta setFeesDelta(d);

		LedgerHeader& ledgerHeader = setFeesDelta.getHeader();

		if (mSetFees.fee)
		{
			if (!trySetFee(app.getMetrics(), db, setFeesDelta))
				return false;
		}

        app.getMetrics().NewMeter({ "op-set-fees", "success", "apply" },
                                  "operation").Mark();

		setFeesDelta.commit();

        return true;
        
    }

	bool SetFeesOpFrame::mustFullRange(FeeEntry const& fee, medida::MetricsRegistry& metrics)
	{
		if (fee.lowerBound == 0 && fee.upperBound == INT64_MAX)
		{
			return true;
		}

		innerResult().code(SET_FEES_MALFORMED_RANGE);
		metrics.NewMeter({ "op-set-fees", "invalid", "invalid-range" }, "operation").Mark();
		return false;
	}

	bool SetFeesOpFrame::mustDefaultSubtype(FeeEntry const & fee, medida::MetricsRegistry & metrics)
	{
		if (fee.subtype == 0)
			return true;

		innerResult().code(SET_FEES_SUB_TYPE_NOT_EXIST);
		metrics.NewMeter({ "op-set-fees", "invalid", "invalid-sub-type-not-exist" }, "operation").Mark();
		return false;
	}

	bool SetFeesOpFrame::mustBaseAsset(FeeEntry const & fee, Application& app)
	{
		if (fee.asset == app.getBaseAsset())
			return true;

		innerResult().code(SET_FEES_SUB_TYPE_NOT_EXIST);
		app.getMetrics().NewMeter({ "op-set-fees", "invalid", "must-be-base-asset" }, "operation").Mark();
		return false;
	}

	bool SetFeesOpFrame::mustValidFeeAmounts(FeeEntry const& fee, medida::MetricsRegistry& metrics)
	{
		if (fee.fixedFee >= 0 && fee.percentFee >= 0 && fee.percentFee <= 100 * ONE)
		{
			if (fee.feeType == REFERRAL_FEE)
			{
				if (fee.percentFee == 100 * ONE)
				{
					return false;
				}
			}
			return true;
		}

		innerResult().code(SET_FEES_INVALID_AMOUNT);
		metrics.NewMeter({ "op-set-fees", "invalid", "invalid-fee-amount" }, "operation").Mark();
		return false;
	}

	bool SetFeesOpFrame::isPaymentFeeValid(FeeEntry const& fee, medida::MetricsRegistry& metrics)
	{
		assert(fee.feeType == PAYMENT_FEE);
		if (!mustValidFeeAmounts(fee, metrics))
			return false;

		if (!mustDefaultSubtype(fee, metrics))
			return false;

		return true;
	}

	bool SetFeesOpFrame::isForfeitFeeValid(FeeEntry const& fee, medida::MetricsRegistry& metrics)
	{
		assert(fee.feeType == FORFEIT_FEE);
		if (!mustValidFeeAmounts(fee, metrics))
			return false;

		return true;
	}

	bool SetFeesOpFrame::isOfferFeeValid(FeeEntry const& fee, medida::MetricsRegistry& metrics)
	{
		assert(fee.feeType == OFFER_FEE);

		if (!mustValidFeeAmounts(fee, metrics))
			return false;

		if (!mustEmptyFixed(fee, metrics))
			return false;

		if (!mustDefaultSubtype(fee, metrics))
			return false;

		return true;
	}

	bool SetFeesOpFrame::isEmissionFeeValid(FeeEntry const& fee, medida::MetricsRegistry& metrics)
	{
		assert(fee.feeType == EMISSION_FEE);

		if (!mustValidFeeAmounts(fee, metrics))
			return false;

		if (!mustEmptyFixed(fee, metrics))
			return false;

		if (fee.subtype != EmissionFeeType::PRIMARY_MARKET && fee.subtype != EmissionFeeType::SECONDARY_MARKET)
		{
			innerResult().code(SET_FEES_MALFORMED);
			metrics.NewMeter({ "op-set-fees", "invalid", "invalid-sub-type" }, "operation").Mark();
			return false;
		}

		return true;
	}


	bool SetFeesOpFrame::isReferralFeeValid(FeeEntry const& fee, Application& app)
	{
		assert(fee.feeType == REFERRAL_FEE);
		auto& metrics = app.getMetrics();

		if (!mustValidFeeAmounts(fee, metrics))
			return false;

		if (!mustFullRange(fee, metrics))
			return false;

		if (!mustEmptyFixed(fee, metrics))
			return false;

		if (!mustDefaultSubtype(fee, metrics))
			return false;

		if (!mustBaseAsset(fee, app))
			return false;

		return true;
	}


	std::unordered_map<AccountID, CounterpartyDetails> SetFeesOpFrame::getCounterpartyDetails(Database & db, LedgerDelta * delta) const
	{
		if (!mSetFees.fee || !mSetFees.fee->accountID)
			return{ };
		return{
			{ *mSetFees.fee->accountID , CounterpartyDetails(getAllAccountTypes(), true, true) }
		};
	}

	SourceDetails SetFeesOpFrame::getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails) const
	{
		return SourceDetails({ MASTER }, mSourceAccount->getHighThreshold(), SIGNER_ASSET_MANAGER);
	}

	bool SetFeesOpFrame::mustEmptyFixed(FeeEntry const& fee, medida::MetricsRegistry& metrics)
	{
		if (fee.fixedFee == 0)
			return true;
		
		innerResult().code(SET_FEES_INVALID_AMOUNT);
		metrics.NewMeter({ "op-set-fees", "invalid", "fixed-fee-must-be-emoty" }, "operation").Mark();
		return false;
	}
    
    bool
    SetFeesOpFrame::doCheckValid(Application& app)
    {
		if (!mSetFees.fee)
		{
			return true;
		}

		if (!AssetFrame::isAssetCodeValid(mSetFees.fee->asset))
		{
			innerResult().code(SET_FEES_INVALID_ASSET);
			app.getMetrics().NewMeter({ "op-set-fees", "invalid", "invalid-asset" }, "operation").Mark();
			return false;
		}

		if (mSetFees.fee->accountID && mSetFees.fee->accountType)
		{
			innerResult().code(SET_FEES_MALFORMED);
			app.getMetrics().NewMeter({ "op-set-fees", "invalid", "malformed-both-set" }, "operation").Mark();
			return false;
		}

		if (mSetFees.fee->lowerBound > mSetFees.fee->upperBound)
		{
			innerResult().code(SET_FEES_MALFORMED);
			app.getMetrics().NewMeter({ "op-set-fees", "invalid", "malformed-boundaries" }, "operation").Mark();
			return false;
		}

		bool isValidFee;
		switch (mSetFees.fee->feeType)
		{
		case PAYMENT_FEE:
			isValidFee = isPaymentFeeValid(*mSetFees.fee, app.getMetrics());
			break;
		case OFFER_FEE:
			isValidFee = isOfferFeeValid(*mSetFees.fee, app.getMetrics());
			break;
		case REFERRAL_FEE:
			isValidFee = isReferralFeeValid(*mSetFees.fee, app);
			break;
		case FORFEIT_FEE:
			isValidFee = isForfeitFeeValid(*mSetFees.fee, app.getMetrics());
			break;
		case EMISSION_FEE:
			isValidFee = isEmissionFeeValid(*mSetFees.fee, app.getMetrics());
			break;
		default:
			innerResult().code(SET_FEES_INVALID_FEE_TYPE);
			app.getMetrics().NewMeter({ "op-set-fees", "invalid", "invalid-operation-type" }, "operation").Mark();
			return false;
		}

		if (!isValidFee)
			return false;


        return true;
    }
}
