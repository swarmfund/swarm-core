// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "transactions/ManageInvoiceOpFrame.h"
#include "database/Database.h"
#include "main/Application.h"
#include "medida/meter.h"
#include "medida/metrics_registry.h"
#include "ledger/LedgerDelta.h"
#include "ledger/BalanceHelper.h"
#include "ledger/InvoiceFrame.h"
#include "ledger/InvoiceHelper.h"

namespace stellar
{
using xdr::operator==;


std::unordered_map<AccountID, CounterpartyDetails> ManageInvoiceOpFrame::getCounterpartyDetails(Database & db, LedgerDelta * delta) const
{
	return{
		{mManageInvoice.sender, CounterpartyDetails({AccountType::GENERAL, AccountType::NOT_VERIFIED}, true, true)}
	};
}

SourceDetails ManageInvoiceOpFrame::getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails) const
{
	return SourceDetails({AccountType::GENERAL, AccountType::NOT_VERIFIED}, mSourceAccount->getMediumThreshold(),
                         static_cast<int32_t >(SignerType::INVOICE_MANAGER), static_cast<int32_t >(BlockReasons::KYC_UPDATE));
}

ManageInvoiceOpFrame::ManageInvoiceOpFrame(Operation const& op, OperationResult& res,
                                     TransactionFrame& parentTx)
    : OperationFrame(op, res, parentTx)
    , mManageInvoice(mOperation.body.manageInvoiceOp())
{
}


bool
ManageInvoiceOpFrame::doApply(Application& app, LedgerDelta& delta,
                           LedgerManager& ledgerManager)
{
    Database& db = ledgerManager.getDatabase();
	innerResult().code(ManageInvoiceResultCode::SUCCESS);

	auto balanceHelper = BalanceHelper::Instance();
    auto balance = balanceHelper->loadBalance(mManageInvoice.receiverBalance, db);

    if (!balance || !(balance->getAccountID() == mSourceAccount->getID()) )
    {
        app.getMetrics().NewMeter({ "op-manage-invoice", "invalid", "balance-not-found" },
                "operation").Mark();
        innerResult().code(ManageInvoiceResultCode::BALANCE_NOT_FOUND);
        return false;
    }

	auto senderBalance = balanceHelper->loadBalance(mManageInvoice.sender, balance->getAsset(), db, &delta);
	if (!senderBalance)
	{
		app.getMetrics().NewMeter({ "op-manage-invoice", "invalid", "sender-balance-not-found" },
			"operation").Mark();
		innerResult().code(ManageInvoiceResultCode::BALANCE_NOT_FOUND);
		return false;
	}

    uint64_t invoiceID;
    if (mManageInvoice.invoiceID == 0)
    {
		auto invoiceHelper = InvoiceHelper::Instance();
        int64_t totalForReceiver = invoiceHelper->countForReceiverAccount(db, getSourceID());
        if (totalForReceiver >= app.getMaxInvoicesForReceiverAccount())
        {
            app.getMetrics().NewMeter({ "op-manage-invoice", "invalid", "too-many-invoices" },
                "operation").Mark();
            innerResult().code(ManageInvoiceResultCode::TOO_MANY_INVOICES);
            return false;
        }
        invoiceID = delta.getHeaderFrame().generateID();

        auto invoiceFrame = std::make_shared<InvoiceFrame>();
        invoiceFrame->getInvoice().sender = mManageInvoice.sender;
        invoiceFrame->getInvoice().receiverAccount = getSourceID();
        invoiceFrame->getInvoice().receiverBalance = mManageInvoice.receiverBalance;
        invoiceFrame->getInvoice().amount = mManageInvoice.amount;
        invoiceFrame->getInvoice().invoiceID = invoiceID;
        EntryHelperProvider::storeAddEntry(delta, db, invoiceFrame->mEntry);
    }
    else
    {
        invoiceID = mManageInvoice.invoiceID;
		auto invoiceHelper = InvoiceHelper::Instance();
        auto invoiceFrame = invoiceHelper->loadInvoice(invoiceID, db);
        if (!invoiceFrame)
        {
            app.getMetrics().NewMeter({ "op-manage-invoice", "invalid", "not-found" },
                "operation").Mark();
            innerResult().code(ManageInvoiceResultCode::NOT_FOUND);
            return false;
        }
        if (invoiceFrame->getState() != InvoiceState::INVOICE_NEEDS_PAYMENT)
        {
            app.getMetrics().NewMeter({ "op-manage-invoice", "invalid", "in-progress" },
                "operation").Mark();
            innerResult().code(ManageInvoiceResultCode::CAN_NOT_DELETE_IN_PROGRESS);
            return false;
        }
        EntryHelperProvider::storeDeleteEntry(delta, db, invoiceFrame->getKey());
    }
    
    

	innerResult().success().invoiceID = invoiceID;
	innerResult().success().asset = balance->getAsset();
	innerResult().success().senderBalance = senderBalance->getBalanceID();

    app.getMetrics().NewMeter({ "op-manage-forfeit-request", "success", "apply" },
            "operation").Mark();
	return true;
}

bool
ManageInvoiceOpFrame::doCheckValid(Application& app)
{
    if (mManageInvoice.amount < 0)
    {
        app.getMetrics().NewMeter({"op-manage-invoice", "invalid", "malformed-negative-amount"},
                         "operation").Mark();
        innerResult().code(ManageInvoiceResultCode::MALFORMED);
        return false;
    }

    if (!((mManageInvoice.amount == 0) ^ (mManageInvoice.invoiceID == 0)))
    {
        app.getMetrics().NewMeter({"op-manage-invoice", "invalid", "malformed-negative-amount"},
                         "operation").Mark();
        innerResult().code(ManageInvoiceResultCode::MALFORMED);
        return false;
    }

    return true;
}

}
