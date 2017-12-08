// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ReviewPaymentRequestOpFrame.h"
#include "database/Database.h"
#include "main/Application.h"
#include "medida/meter.h"
#include "medida/metrics_registry.h"
#include "ledger/PaymentRequestFrame.h"
#include "ledger/PaymentRequestHelper.h"
#include "ledger/AccountFrame.h"
#include "ledger/AccountHelper.h"
#include "ledger/BalanceHelper.h"
#include "ledger/InvoiceFrame.h"
#include "ledger/InvoiceHelper.h"


namespace stellar
{
using xdr::operator==;


std::unordered_map<AccountID, CounterpartyDetails> ReviewPaymentRequestOpFrame::getCounterpartyDetails(Database & db, LedgerDelta * delta) const
{
	// on reject we do not care about counterparties
	if (!mReviewPaymentRequest.accept)
		return{};

	auto paymentRequestHelper = PaymentRequestHelper::Instance();
	auto request = paymentRequestHelper->loadPaymentRequest(mReviewPaymentRequest.paymentID, db, delta);
	if (!request)
		return{};
	std::unordered_map<AccountID, CounterpartyDetails> result;

	auto balanceHelper = BalanceHelper::Instance();
	auto sourceBalanceFrame = balanceHelper->loadBalance(request->getSourceBalance(), db);
	if (sourceBalanceFrame)
	{
		result.insert({sourceBalanceFrame->getAccountID(), CounterpartyDetails({ AccountType::GENERAL, AccountType::OPERATIONAL, AccountType::COMMISSION }, false, true) });
	}

	auto destBalance = request->getPaymentRequest().destinationBalance;
	if (destBalance)
	{
		auto destBalanceFrame = balanceHelper->loadBalance(*request->getPaymentRequest().destinationBalance, db);
		if (destBalanceFrame)
		{
			result.insert({ destBalanceFrame->getAccountID(), CounterpartyDetails({ AccountType::NOT_VERIFIED, AccountType::GENERAL, AccountType::OPERATIONAL, AccountType::COMMISSION }, true, true) });
		}
	}
	return result;
}

SourceDetails ReviewPaymentRequestOpFrame::getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails) const
{
	return SourceDetails({AccountType::MASTER}, mSourceAccount->getMediumThreshold(), static_cast<int32_t>(SignerType::PAYMENT_OPERATOR));
}

ReviewPaymentRequestOpFrame::ReviewPaymentRequestOpFrame(Operation const& op, OperationResult& res,
                                     TransactionFrame& parentTx)
    : OperationFrame(op, res, parentTx)
    , mReviewPaymentRequest(mOperation.body.reviewPaymentRequestOp())
{
}

bool
ReviewPaymentRequestOpFrame::doApply(Application& app, LedgerDelta& delta,
                           LedgerManager& ledgerManager)
{
    Database& db = ledgerManager.getDatabase();


    AccountManager accountManager(app, db, delta, ledgerManager);

	innerResult().code(ReviewPaymentRequestResultCode::SUCCESS);

	auto paymentRequestHelper = PaymentRequestHelper::Instance();
    auto request = paymentRequestHelper->loadPaymentRequest(mReviewPaymentRequest.paymentID, db, &delta);
    if (!request)
    {
        app.getMetrics().NewMeter({ "op-review-payment-request", "invalid", "not-found" },
            "operation").Mark();
        innerResult().code(ReviewPaymentRequestResultCode::NOT_FOUND);
        return false;
    }

	auto balanceHelper = BalanceHelper::Instance();
    auto sourceBalanceFrame = balanceHelper->loadBalance(request->getSourceBalance(), db);
    assert(sourceBalanceFrame);

	auto accountHelper = AccountHelper::Instance();
	auto sourceBalanceAccount = accountHelper->loadAccount(delta, sourceBalanceFrame->getAccountID(), db);
	assert(sourceBalanceAccount);
    
    if (mReviewPaymentRequest.accept)
    {    
        if (request->getPaymentRequest().destinationBalance)
        {
            auto destBalanceFrame = balanceHelper->loadBalance(*request->getPaymentRequest().destinationBalance, db);
            assert(destBalanceFrame);
			auto destBalanceAccount = accountHelper->loadAccount(delta, destBalanceFrame->getAccountID(), db);
			assert(sourceBalanceAccount);
            if (!destBalanceFrame->addBalance(request->getDestinationReceive()))
            {
                app.getMetrics().NewMeter({ "op-review-payment-request", "failure", "full-line" }, "operation").Mark();
                innerResult().code(ReviewPaymentRequestResultCode::LINE_FULL);
                return false;
            }
            EntryHelperProvider::storeChangeEntry(delta, db, destBalanceFrame->mEntry);
        }

        assert(sourceBalanceFrame->addLocked(-request->getSourceSend()));
        auto commissionBalanceFrame = balanceHelper->loadBalance(app.getCommissionID(),
            sourceBalanceFrame->getAsset(), app.getDatabase(), &delta);
        assert(commissionBalanceFrame);
		auto commissionAccount = accountHelper->loadAccount(delta, commissionBalanceFrame->getAccountID(), db);

        auto fee = request->getSourceSend() - request->getDestinationReceive();
        if (fee < 0)
        {
            CLOG(ERROR, "ReviewPaymentRequest") << "sourceSend less than destinationReceive";
            throw std::runtime_error("fee must be positive or zero");
        }

        if (!commissionBalanceFrame->addBalance(fee))
        {
            app.getMetrics().NewMeter({ "op-review-payment-request", "failure", "commission-full-line" }, "operation").Mark();
            innerResult().code(ReviewPaymentRequestResultCode::LINE_FULL);
            return false;
        }
                
        EntryHelperProvider::storeChangeEntry(delta, db, sourceBalanceFrame->mEntry);
		EntryHelperProvider::storeChangeEntry(delta, db, commissionBalanceFrame->mEntry);
		EntryHelperProvider::storeDeleteEntry(delta, db, request->getKey());
        innerResult().reviewPaymentResponse().state = PaymentState::PROCESSED;
        
        tryProcessInvoice(request->getInvoiceID(), delta, db);
	}
    else
    {
        if (request->getPaymentRequest().destinationBalance)
        {
            auto destBalanceFrame = balanceHelper->loadBalance(*request->getPaymentRequest().destinationBalance, db);
			assert(destBalanceFrame);
        }

        if (!accountManager.revertRequest(sourceBalanceAccount, sourceBalanceFrame,
            request->getSourceSend(),
            request->getSourceSendUniversal(), request->getCreatedAt()))
        {
            app.getMetrics().NewMeter({ "op-review-payment-request", "failure", "line-full" }, "operation").Mark();
            innerResult().code(ReviewPaymentRequestResultCode::LINE_FULL);
            return false;
        }

        EntryHelperProvider::storeDeleteEntry(delta, ledgerManager.getDatabase(), request->getKey());

		EntryHelperProvider::storeChangeEntry(delta, db, sourceBalanceFrame->mEntry);
        innerResult().reviewPaymentResponse().state = PaymentState::REJECTED;

        tryProcessInvoice(request->getInvoiceID(), delta, db);
    }

    app.getMetrics().NewMeter({"op-review-payment-request", "success", "apply"}, "operation").Mark();
	return true;
}

void ReviewPaymentRequestOpFrame::tryProcessInvoice(uint64* invoiceID,
    LedgerDelta& delta, Database& db)
{
    if (invoiceID)
    {
        if (*invoiceID > 100)
            return;
		auto invoiceHelper = InvoiceHelper::Instance();
        auto invoiceFrame = invoiceHelper->loadInvoice(*invoiceID, db);
        assert(invoiceFrame);
        EntryHelperProvider::storeDeleteEntry(delta, db, invoiceFrame->getKey());
        innerResult().reviewPaymentResponse().relatedInvoiceID.activate() = *invoiceID;
    }
}

bool
ReviewPaymentRequestOpFrame::doCheckValid(Application& app)
{
    return true;
}

}
