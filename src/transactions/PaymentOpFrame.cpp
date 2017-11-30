// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "transactions/PaymentOpFrame.h"
#include "ledger/ReferenceFrame.h"
#include "ledger/LedgerDelta.h"
#include "database/Database.h"
#include "medida/meter.h"
#include "medida/metrics_registry.h"
#include "main/Application.h"

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
	mSourceBalance = BalanceFrame::loadBalance(mPayment.sourceBalanceID, db, &delta);
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

	mDestBalance = BalanceFrame::loadBalance(mPayment.destinationBalanceID, db, &delta);
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

	mDestAccount = AccountFrame::loadAccount(mDestBalance->getAccountID(), db);
	if (!mDestAccount)
		throw std::runtime_error("Invalid database state - can't load account for balance");

	return true;
}

std::unordered_map<AccountID, CounterpartyDetails> PaymentOpFrame::getCounterpartyDetails(Database & db, LedgerDelta * delta) const
{
	auto targetBalance = BalanceFrame::loadBalance(mPayment.destinationBalanceID, db, delta);
	// counterparty does not exists, error will be returned from do apply
	if (!targetBalance)
		return{};
	return{
		{ targetBalance->getAccountID(), CounterpartyDetails({ AccountType::NOT_VERIFIED, AccountType::GENERAL, AccountType::OPERATIONAL, AccountType::COMMISSION }, true, false) }
	};
}

SourceDetails PaymentOpFrame::getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails) const
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
	std::vector<AccountType> allowedAccountTypes = { AccountType::NOT_VERIFIED, AccountType::GENERAL, AccountType::OPERATIONAL, AccountType::COMMISSION };

	return SourceDetails(allowedAccountTypes, mSourceAccount->getMediumThreshold(), signerType);
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
	auto feeFrame = FeeFrame::loadForAccount(FeeType::PAYMENT_FEE, assetCode, FeeFrame::SUBTYPE_ANY, accountFrame, amount, db, &delta);
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
	// TODO fix me
	//asset->checkPolicy(AssetPolicy::TRANSFERABLE);
	return true;
}

bool
PaymentOpFrame::doApply(Application& app, LedgerDelta& delta,
                        LedgerManager& ledgerManager)
{
    app.getMetrics().NewMeter({"op-payment", "success", "apply"}, "operation").Mark();
    innerResult().code(PaymentResultCode::SUCCESS);
    
    Database& db = ledgerManager.getDatabase();
    
	if (!tryLoadBalances(app, db, delta))
	{
		// failed to load balances
		return false;
	}

    auto assetFrame = AssetFrame::loadAsset(mSourceBalance->getAsset(), db);
    assert(assetFrame);
    if (!isAllowedToTransfer(db, assetFrame))
    {
        app.getMetrics().NewMeter({ "op-payment", "failure", "not-allowed-by-asset-policy" }, "operation").Mark();
        innerResult().code(PaymentResultCode::NOT_ALLOWED_BY_ASSET_POLICY);
        return false;
    }


	if (!checkFees(app, db, delta))
	{
		return false;
	}
    
    if (mPayment.invoiceReference)
    {
        if (!processInvoice(app, delta, db))
            return false;
        auto invoiceReference = *mPayment.invoiceReference;
        if (!invoiceReference.accept)
        {
            assert(invoiceFrame);
            invoiceFrame->storeDelete(delta, db);
            return true;
        }
    }

    AccountManager accountManager(app, db, delta, ledgerManager);

    uint64 paymentID = delta.getHeaderFrame().generateID();

    if (mPayment.reference.size() != 0)
    {
		AccountID sourceAccountID = mSourceAccount->getID();
        if (ReferenceFrame::exists(db, mPayment.reference, sourceAccountID))
        {
            app.getMetrics().NewMeter({ "op-payment", "failure", "reference-duplication" }, "operation").Mark();
            innerResult().code(PaymentResultCode::REFERENCE_DUPLICATION);
            return false;
        }
        createReferenceEntry(mPayment.reference, &delta, db);
    }

    int64 sourceSentUniversal;
    auto transferResult = accountManager.processTransfer(mSourceAccount, mSourceBalance,
        sourceSent, sourceSentUniversal);

    if (!processBalanceChange(app, transferResult))
        return false;

	if (invoiceFrame)
		invoiceFrame->storeDelete(delta, db);

	if (!mDestBalance->addBalance(destReceived))
	{
		app.getMetrics().NewMeter({ "op-payment", "failure", "full-line" }, "operation").Mark();
		innerResult().code(PaymentResultCode::LINE_FULL);
		return false;
	}

	auto commissionBalanceFrame = BalanceFrame::loadBalance(app.getCommissionID(),
		mSourceBalance->getAsset(), app.getDatabase(), &delta);
	assert(commissionBalanceFrame);
	auto commissionAccount = AccountFrame::loadAccount(delta, commissionBalanceFrame->getAccountID(), db);
	auto feeData = mPayment.feeData;
	auto totalFee = feeData.sourceFee.paymentFee + feeData.sourceFee.fixedFee +
		feeData.destinationFee.paymentFee + feeData.destinationFee.fixedFee;

	if (!commissionBalanceFrame->addBalance(totalFee))
	{
		app.getMetrics().NewMeter({ "op-payment", "failure", "commission-full-line" }, "operation").Mark();
		innerResult().code(PaymentResultCode::LINE_FULL);
		return false;
	}
	commissionBalanceFrame->storeChange(delta, db);

    innerResult().paymentResponse().destination = mDestBalance->getAccountID();
    innerResult().paymentResponse().asset = mDestBalance->getAsset();
    mSourceBalance->storeChange(delta, db);
	mDestBalance->storeChange(delta, db);

    innerResult().paymentResponse().paymentID = paymentID;

    return true;
}

bool
PaymentOpFrame::processInvoice(Application& app, LedgerDelta& delta, Database& db)
{
    assert(mPayment.invoiceReference);
    auto invoiceReference = *mPayment.invoiceReference;
    
    invoiceFrame = InvoiceFrame::loadInvoice(invoiceReference.invoiceID, db);
    if (!invoiceFrame)
    {
        app.getMetrics().NewMeter({ "op-payment", "failure", "invoice-not-found" }, "operation").Mark();
        innerResult().code(PaymentResultCode::INVOICE_NOT_FOUND);
        return false;
    }

    if (invoiceFrame->getState() != InvoiceState::INVOICE_NEEDS_PAYMENT)
    {
        app.getMetrics().NewMeter({ "op-payment", "failure", "invoice-already-paid" }, "operation").Mark();
        innerResult().code(PaymentResultCode::INVOICE_ALREADY_PAID);
        return false;
    }
    
    if (!(invoiceFrame->getSender() == getSourceID()))
    {
        app.getMetrics().NewMeter({ "op-payment", "failure", "invoice-account-mismatch" }, "operation").Mark();
        innerResult().code(PaymentResultCode::INVOICE_ACCOUNT_MISMATCH);
        return false;
    }

    if (invoiceReference.accept)
    {
        if (invoiceFrame->getAmount() != destReceived)
        {
            app.getMetrics().NewMeter({ "op-payment", "failure", "invoice-wrong-amount" }, "operation").Mark();
            innerResult().code(PaymentResultCode::INVOICE_WRONG_AMOUNT);
            return false;
        }
        if (!(invoiceFrame->getReceiverBalance() == mPayment.destinationBalanceID))
        {
            app.getMetrics().NewMeter({ "op-payment", "failure", "invoice-balance-mismatch" }, "operation").Mark();
            innerResult().code(PaymentResultCode::INVOICE_BALANCE_MISMATCH);
            return false;
        }
    }
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
