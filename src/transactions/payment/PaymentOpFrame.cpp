// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "PaymentOpFrame.h"
#include "database/Database.h"
#include "ledger/AccountHelper.h"
#include "ledger/AssetHelper.h"
#include "ledger/BalanceHelper.h"
#include "ledger/FeeHelper.h"
#include "ledger/LedgerDelta.h"
#include "ledger/LedgerHeaderFrame.h"
#include "ledger/ReferenceFrame.h"
#include "ledger/ReferenceHelper.h"
#include "ledger/StorageHelper.h"
#include "main/Application.h"
#include "medida/meter.h"
#include "medida/metrics_registry.h"

namespace stellar
{

using namespace std;
using xdr::operator==;

PaymentOpFrame::PaymentOpFrame(Operation const& op, OperationResult& res,
                               TransactionFrame& parentTx)
    : OperationFrame(op, res, parentTx), mPayment(mOperation.body.paymentOp())
{
}


bool PaymentOpFrame::tryLoadBalances(Application& app, Database& db, LedgerDelta& delta)
{
	auto balanceHelper = BalanceHelper::Instance();
	mSourceBalance = balanceHelper->loadBalance(mPayment.sourceBalanceID, db, &delta);
	if (!mSourceBalance)
	{
		app.getMetrics().NewMeter({ "op-payment", "failure", "src-balance-not-found-fee" }, "operation").Mark();
		innerResult().code(PaymentResultCode::SRC_BALANCE_NOT_FOUND);
		return false;
	}

	if (!(mSourceBalance->getAccountID() == getSourceID()))
	{
		app.getMetrics().NewMeter({ "op-payment", "failure", "account-mismatch" }, "operation").Mark();
		innerResult().code(PaymentResultCode::BALANCE_ACCOUNT_MISMATCHED);
		return false;
	}

	mDestBalance = balanceHelper->loadBalance(mPayment.destinationBalanceID, db, &delta);
	if (!mDestBalance)
	{
		app.getMetrics().NewMeter({ "op-payment", "failure", "balance-not-found" }, "operation").Mark();
		innerResult().code(PaymentResultCode::BALANCE_NOT_FOUND);
		return false;
	}

	if (!(mSourceBalance->getAsset() == mDestBalance->getAsset()))
	{
		app.getMetrics().NewMeter({ "op-payment", "failure", "assets-mismatch" }, "operation").Mark();
		innerResult().code(PaymentResultCode::BALANCE_ASSETS_MISMATCHED);
		return false;
	}

	auto accountHelper = AccountHelper::Instance();
	mDestAccount = accountHelper->loadAccount(mDestBalance->getAccountID(), db);
	if (!mDestAccount)
		throw std::runtime_error("Invalid database state - can't load account for balance");

	return true;
}

std::unordered_map<AccountID, CounterpartyDetails> PaymentOpFrame::getCounterpartyDetails(Database & db, LedgerDelta * delta) const
{
	auto balanceHelper = BalanceHelper::Instance();
	auto targetBalance = balanceHelper->loadBalance(mPayment.destinationBalanceID, db, delta);
	// counterparty does not exists, error will be returned from do apply
	if (!targetBalance)
		return{};
	return{
		{ targetBalance->getAccountID(), CounterpartyDetails({ AccountType::NOT_VERIFIED, AccountType::GENERAL, AccountType::OPERATIONAL,
                                                               AccountType::COMMISSION, AccountType::SYNDICATE, AccountType::EXCHANGE,
                                                               AccountType::ACCREDITED_INVESTOR, AccountType::INSTITUTIONAL_INVESTOR,
                                                               AccountType::VERIFIED},
                                                             true, false) }
	};
}

SourceDetails PaymentOpFrame::getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
                                                      int32_t ledgerVersion) const
{
	int32_t signerType = static_cast<int32_t >(SignerType::BALANCE_MANAGER);
	switch (mSourceAccount->getAccountType())
	{
	case AccountType::OPERATIONAL:
		signerType = static_cast<int32_t >(SignerType::OPERATIONAL_BALANCE_MANAGER);
		break;
	case AccountType::COMMISSION:
		signerType = static_cast<int32_t >(SignerType::COMMISSION_BALANCE_MANAGER);
		break;
	default:
		break;
	}
        std::vector<AccountType> allowedAccountTypes;
    if (ledgerVersion >= int32_t(LedgerVersion::USE_KYC_LEVEL))
    {
        allowedAccountTypes = { AccountType::NOT_VERIFIED, AccountType::GENERAL, AccountType::OPERATIONAL,
                                AccountType::COMMISSION, AccountType::SYNDICATE, AccountType::EXCHANGE,
                                AccountType::ACCREDITED_INVESTOR, AccountType::INSTITUTIONAL_INVESTOR,
                                AccountType::VERIFIED};
    }
	return SourceDetails(allowedAccountTypes, mSourceAccount->getMediumThreshold(), signerType,
						 static_cast<int32_t>(BlockReasons::TOO_MANY_KYC_UPDATE_REQUESTS) |
                         static_cast<uint32_t>(BlockReasons::WITHDRAWAL));
}

bool PaymentOpFrame::isRecipeintFeeNotRequired(Database& db)
{
	if (mSourceAccount->getAccountType() == AccountType::COMMISSION)
		return true;

	return false;
}

bool PaymentOpFrame::checkFees(Application& app, Database& db, LedgerDelta& delta)
{
	bool areFeesNotRequired = isSystemAccountType(mSourceAccount->getAccountType());
	if (areFeesNotRequired)
		return true;

	AssetCode asset = mSourceBalance->getAsset();
	if (!isTransferFeeMatch(mSourceAccount, asset, mPayment.feeData.sourceFee, mPayment.amount, db, delta))
	{
		app.getMetrics().NewMeter({ "op-payment", "failure", "source-mismatched-fee" }, "operation").Mark();
		innerResult().code(PaymentResultCode::FEE_MISMATCHED);
		return false;
	}

	if (isRecipeintFeeNotRequired(db))
	{
		return true;
	}

	if (!isTransferFeeMatch(mDestAccount, asset, mPayment.feeData.destinationFee, mPayment.amount, db, delta))
	{
		app.getMetrics().NewMeter({ "op-payment", "failure", "dest-mismatched-fee" }, "operation").Mark();
		innerResult().code(PaymentResultCode::FEE_MISMATCHED);
		return false;
	}

	return true;
}

bool PaymentOpFrame::checkFeesV1(Application& app, Database& db, LedgerDelta& delta)
{
	if (isRecipeintFeeNotRequired(db))
	{
		return true;
	}
	
	AssetCode asset = mSourceBalance->getAsset();
	
    if (!isTransferFeeMatch(mSourceAccount, asset, mPayment.feeData.sourceFee, mPayment.amount, db, delta))
	{
		app.getMetrics().NewMeter({ "op-payment", "failure", "source-mismatched-fee" }, "operation").Mark();
		innerResult().code(PaymentResultCode::FEE_MISMATCHED);
        return false;
	}

    if (!isTransferFeeMatch(mDestAccount, asset, mPayment.feeData.destinationFee, mPayment.amount, db, delta))
	{
		app.getMetrics().NewMeter({ "op-payment", "failure", "dest-mismatched-fee" }, "operation").Mark();
		innerResult().code(PaymentResultCode::FEE_MISMATCHED);
        return false;
	}

	return true;
}

bool PaymentOpFrame::isTransferFeeMatch(AccountFrame::pointer accountFrame, AssetCode const& assetCode, FeeData const& feeData, int64_t const& amount, Database& db, LedgerDelta& delta)
{
	auto feeHelper = FeeHelper::Instance();
	auto feeFrame = feeHelper->loadForAccount(FeeType::PAYMENT_FEE, assetCode, FeeFrame::SUBTYPE_ANY, accountFrame, amount, db, &delta);
	// if we do not have any fee frame - any fee is valid
	if (!feeFrame)
		return true;

    int64_t requiredPaymentFee = feeFrame->calculatePercentFee(amount);
    int64_t requiredFixedFee = feeFrame->getFee().fixedFee;
    return feeData.paymentFee == requiredPaymentFee && feeData.fixedFee == requiredFixedFee;
}

bool PaymentOpFrame::calculateSourceDestAmount()
{
    auto feeData = mPayment.feeData;
	destReceived = mPayment.amount;
	sourceSent = mPayment.amount + feeData.sourceFee.fixedFee;
	if (sourceSent <= 0)
	{
		return false;
	}
    sourceSent += feeData.sourceFee.paymentFee;
	if (sourceSent <= 0)
	{
		return false;
	}    

	if (feeData.sourcePaysForDest)
	{
		sourceSent += feeData.destinationFee.paymentFee;
		if (sourceSent <= 0)
		{
			return false;
		}
		sourceSent += feeData.destinationFee.fixedFee;
		if (sourceSent <= 0)
		{
			return false;
		}
		return true;
	}
	else
    {
        destReceived -= feeData.destinationFee.paymentFee;
        if (destReceived <= 0)
        {
            return false;
        }
        destReceived -= feeData.destinationFee.fixedFee;
        if (destReceived <= 0)
        {
            return false;
        }
    }

	return true;
}

bool PaymentOpFrame::isAllowedToTransfer(Database& db, AssetFrame::pointer asset)
{
	return asset->isPolicySet(AssetPolicy::TRANSFERABLE);
}

bool PaymentOpFrame::processFees(Application& app, LedgerManager& lm, LedgerDelta& delta, Database& db)
{
    if (lm.shouldUse(LedgerVersion::AUTO_CREATE_COMMISSION_BALANCE_ON_TRANSFER))
    {
        return processFees_v2(app, delta, db);
    }

    return processFees_v1(app, delta, db);
}

bool PaymentOpFrame::processFees_v1(Application& app, LedgerDelta& delta,
    Database& db)
{
    auto balanceHelper = BalanceHelper::Instance();
    auto commissionBalanceFrame = balanceHelper->loadBalance(app.getCommissionID(),
        mSourceBalance->getAsset(), app.getDatabase(), &delta);
    if (!commissionBalanceFrame)
    {
        throw runtime_error("Unexpected state: failed to load commission balance for transfer");
    }

    auto accountHelper = AccountHelper::Instance();
    auto commissionAccount = accountHelper->loadAccount(delta, commissionBalanceFrame->getAccountID(), db);
    auto feeData = mPayment.feeData;
    auto totalFee = feeData.sourceFee.paymentFee + feeData.sourceFee.fixedFee +
        feeData.destinationFee.paymentFee + feeData.destinationFee.fixedFee;

    if (!commissionBalanceFrame->addBalance(totalFee))
    {
        app.getMetrics().NewMeter({ "op-payment", "failure", "commission-full-line" }, "operation").Mark();
        innerResult().code(PaymentResultCode::LINE_FULL);
        return false;
    }
    EntryHelperProvider::storeChangeEntry(delta, db, commissionBalanceFrame->mEntry);
    return true;
}

bool PaymentOpFrame::processFees_v2(Application& app, LedgerDelta& delta,
    Database& db)
{
    uint64_t totalFees;
    if (!safeSum(totalFees, std::vector<uint64_t>{uint64_t(mPayment.feeData.sourceFee.fixedFee), uint64_t(mPayment.feeData.sourceFee.paymentFee),
        uint64_t(mPayment.feeData.destinationFee.fixedFee), uint64_t(mPayment.feeData.destinationFee.paymentFee)}))
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected state: failed to calculate total sum of fees to be charged - overflow";
        throw std::runtime_error("Total sum of fees to be charged - overlows");
    }

    if (totalFees == 0)
    {
        return true;
    }

    auto balance = AccountManager::loadOrCreateBalanceFrameForAsset(app.getCommissionID(), mSourceBalance->getAsset(), db, delta);
    if (!balance->tryFundAccount(totalFees))
    {
        app.getMetrics().NewMeter({ "op-payment", "failure", "commission-full-line" }, "operation").Mark();
        innerResult().code(PaymentResultCode::LINE_FULL);
        return false;
    }

    EntryHelperProvider::storeChangeEntry(delta, db, balance->mEntry);
    return true;
}

bool
PaymentOpFrame::doApply(Application& app, StorageHelper& storageHelper,
                        LedgerManager& ledgerManager)
{
    if (ledgerManager.shouldUse(LedgerVersion::USE_ONLY_PAYMENT_V2))
    {
        innerResult().code(PaymentResultCode::PAYMENT_V1_NO_LONGER_SUPPORTED);
        return false;
    }

    app.getMetrics().NewMeter({"op-payment", "success", "apply"}, "operation").Mark();
    innerResult().code(PaymentResultCode::SUCCESS);
    
    Database& db = ledgerManager.getDatabase();
    LedgerDelta& delta = storageHelper.getLedgerDelta();
    
	if (!tryLoadBalances(app, db, delta))
	{
		// failed to load balances
		return false;
	}

	auto assetHelper = AssetHelper::Instance();
    auto assetFrame = assetHelper->loadAsset(mSourceBalance->getAsset(), db);
    assert(assetFrame);
    if (!isAllowedToTransfer(db, assetFrame) ||
        AccountManager::isAllowedToReceive(mPayment.destinationBalanceID, db) != AccountManager::SUCCESS)
    {
        app.getMetrics().NewMeter({ "op-payment", "failure", "not-allowed-by-asset-policy" }, "operation").Mark();
        innerResult().code(PaymentResultCode::NOT_ALLOWED_BY_ASSET_POLICY);
        return false;
    }

	if (!checkFees(app, db, delta))
	{
		return false;
	}

    AccountManager accountManager(app, db, delta, ledgerManager);

    uint64 paymentID = delta.getHeaderFrame().generateID(LedgerEntryType::PAYMENT_REQUEST);

    if (mPayment.reference.size() != 0)
    {
		AccountID sourceAccountID = mSourceAccount->getID();
		
		auto referenceHelper = ReferenceHelper::Instance();
        if (referenceHelper->exists(db, mPayment.reference, sourceAccountID))
        {
            app.getMetrics().NewMeter({ "op-payment", "failure", "reference-duplication" }, "operation").Mark();
            innerResult().code(PaymentResultCode::REFERENCE_DUPLICATION);
            return false;
        }
        createReferenceEntry(mPayment.reference, storageHelper);
    }

    int64 sourceSentUniversal;
    auto transferResult = accountManager.processTransfer(mSourceAccount, mSourceBalance,
        sourceSent, sourceSentUniversal);

    if (!processBalanceChange(app, transferResult))
        return false;

	if (!mDestBalance->addBalance(destReceived))
	{
		app.getMetrics().NewMeter({ "op-payment", "failure", "full-line" }, "operation").Mark();
		innerResult().code(PaymentResultCode::LINE_FULL);
		return false;
	}

	if (!processFees(app, ledgerManager, delta, db))
	{
            return false;
	}

    innerResult().paymentResponse().destination = mDestBalance->getAccountID();
    innerResult().paymentResponse().asset = mDestBalance->getAsset();
    EntryHelperProvider::storeChangeEntry(delta, db, mSourceBalance->mEntry);
	EntryHelperProvider::storeChangeEntry(delta, db, mDestBalance->mEntry);

    innerResult().paymentResponse().paymentID = paymentID;

    return true;
}

bool
PaymentOpFrame::processBalanceChange(Application& app, AccountManager::Result balanceChangeResult)
{
    if (balanceChangeResult == AccountManager::Result::UNDERFUNDED)
    {
        app.getMetrics().NewMeter({ "op-payment", "failure", "underfunded" }, "operation").Mark();
        innerResult().code(PaymentResultCode::UNDERFUNDED);
        return false;
    }

    if (balanceChangeResult == AccountManager::Result::STATS_OVERFLOW)
    {
        app.getMetrics().NewMeter({ "op-payment", "failure", "stats-overflow" }, "operation").Mark();
        innerResult().code(PaymentResultCode::STATS_OVERFLOW);
        return false;
    }

    if (balanceChangeResult == AccountManager::Result::LIMITS_EXCEEDED)
    {
        app.getMetrics().NewMeter({ "op-payment", "failure", "limits-exceeded" }, "operation").Mark();
        innerResult().code(PaymentResultCode::LIMITS_EXCEEDED);
        return false;
    }
    return true;
}

bool
PaymentOpFrame::doCheckValid(Application& app)
{
    if (mPayment.amount <= 0)
    {
        app.getMetrics().NewMeter({"op-payment", "invalid", "malformed-negative-amount"},
                         "operation").Mark();
        innerResult().code(PaymentResultCode::MALFORMED);
        return false;
    }

	bool areFeesValid = isFeeValid(mPayment.feeData.sourceFee) && isFeeValid(mPayment.feeData.destinationFee);
    if (!areFeesValid)
    {
        app.getMetrics().NewMeter({"op-payment", "invalid", "malformed-invalid-fee"},
                         "operation").Mark();
        innerResult().code(PaymentResultCode::MALFORMED);
        return false;
    }

	if (!calculateSourceDestAmount())
	{
		app.getMetrics().NewMeter({ "op-payment", "invalid", "malformed-overflow-amount" },
			"operation").Mark();
		innerResult().code(PaymentResultCode::MALFORMED);
		return false;
	}

	if (mPayment.sourceBalanceID == mPayment.destinationBalanceID)
	{
		app.getMetrics().NewMeter({ "op-payment", "invalid", "sending-to-self" }, "operation")
			.Mark();
		innerResult().code(PaymentResultCode::MALFORMED);
		return false;
	}

    return true;
}
}
