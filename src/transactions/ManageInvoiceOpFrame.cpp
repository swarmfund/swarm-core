// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "transactions/ManageInvoiceOpFrame.h"
#include "database/Database.h"
#include "main/Application.h"
#include "medida/meter.h"
#include "medida/metrics_registry.h"
#include "ledger/LedgerDelta.h"
#include "ledger/InvoiceFrame.h"

namespace stellar
{
using xdr::operator==;


std::unordered_map<AccountID, CounterpartyDetails> ManageInvoiceOpFrame::getCounterpartyDetails(Database & db, LedgerDelta * delta) const
{
	return{
		{mManageInvoice.sender, CounterpartyDetails({GENERAL, NOT_VERIFIED}, true, true)}
	};
}

SourceDetails ManageInvoiceOpFrame::getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails) const
{
	return SourceDetails({GENERAL, NOT_VERIFIED}, mSourceAccount->getMediumThreshold(), SIGNER_INVOICE_MANAGER, BlockReasons::KYC_UPDATE);
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
	innerResult().code(MANAGE_INVOICE_SUCCESS);

    auto balance = BalanceFrame::loadBalance(mManageInvoice.receiverBalance, db);

    if (!balance || !(balance->getAccountID() == mSourceAccount->getID()) )
    {
        app.getMetrics().NewMeter({ "op-manage-invoice", "invalid", "balance-not-found" },
                "operation").Mark();
        innerResult().code(MANAGE_INVOICE_BALANCE_NOT_FOUND);
        return false;
    }

	auto senderBalance = BalanceFrame::loadBalance(mManageInvoice.sender, balance->getAsset(), db, &delta);
	if (!senderBalance)
	{
		app.getMetrics().NewMeter({ "op-manage-invoice", "invalid", "sender-balance-not-found" },
			"operation").Mark();
		innerResult().code(MANAGE_INVOICE_BALANCE_NOT_FOUND);
		return false;
	}

    uint64_t invoiceID;
    if (mManageInvoice.invoiceID == 0)
    {
        int64_t totalForReceiver = InvoiceFrame::countForReceiverAccount(db, getSourceID());
        if (totalForReceiver >= app.getMaxInvoicesForReceiverAccount())
        {
            app.getMetrics().NewMeter({ "op-manage-invoice", "invalid", "too-many-invoices" },
                "operation").Mark();
            innerResult().code(MANAGE_INVOICE_TOO_MANY_INVOICES);
            return false;
        }
        invoiceID = delta.getHeaderFrame().generateID();

        auto invoiceFrame = std::make_shared<InvoiceFrame>();
        invoiceFrame->getInvoice().sender = mManageInvoice.sender;
        invoiceFrame->getInvoice().receiverAccount = getSourceID();
        invoiceFrame->getInvoice().receiverBalance = mManageInvoice.receiverBalance;
        invoiceFrame->getInvoice().amount = mManageInvoice.amount;
        invoiceFrame->getInvoice().invoiceID = invoiceID;
        invoiceFrame->storeAdd(delta, db);
    }
    else
    {
        invoiceID = mManageInvoice.invoiceID;
        auto invoiceFrame = InvoiceFrame::loadInvoice(invoiceID, db);
        if (!invoiceFrame)
        {
            app.getMetrics().NewMeter({ "op-manage-invoice", "invalid", "not-found" },
                "operation").Mark();
            innerResult().code(MANAGE_INVOICE_NOT_FOUND);
            return false;
        }
        if (invoiceFrame->getState() != INVOICE_NEEDS_PAYMENT)
        {
            app.getMetrics().NewMeter({ "op-manage-invoice", "invalid", "in-progress" },
                "operation").Mark();
            innerResult().code(MANAGE_INVOICE_CAN_NOT_DELETE_IN_PROGRESS);
            return false;
        }
        invoiceFrame->storeDelete(delta, db);
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
        innerResult().code(MANAGE_INVOICE_MALFORMED);
        return false;
    }

    if (!((mManageInvoice.amount == 0) ^ (mManageInvoice.invoiceID == 0)))
    {
        app.getMetrics().NewMeter({"op-manage-invoice", "invalid", "malformed-negative-amount"},
                         "operation").Mark();
        innerResult().code(MANAGE_INVOICE_MALFORMED);
        return false;
    }

    return true;
}

}
