// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include <ledger/ReviewableRequestFrame.h>
#include "transactions/ManageInvoiceRequestOpFrame.h"
#include "database/Database.h"
#include "main/Application.h"
#include "medida/meter.h"
#include <crypto/SHA.h>
#include <ledger/ReviewableRequestHelper.h>
#include "medida/metrics_registry.h"
#include "ledger/LedgerDelta.h"
#include "ledger/BalanceHelper.h"

namespace stellar
{
using xdr::operator==;


std::unordered_map<AccountID, CounterpartyDetails>
ManageInvoiceRequestOpFrame::getCounterpartyDetails(Database & db, LedgerDelta * delta) const
{
	return{
		{mSourceAccount->getID(),
                CounterpartyDetails({AccountType::GENERAL, AccountType::NOT_VERIFIED, AccountType::EXCHANGE,
                                     AccountType::ACCREDITED_INVESTOR, AccountType::INSTITUTIONAL_INVESTOR,
                                     AccountType::VERIFIED}, true, true)}
	};
}

SourceDetails
ManageInvoiceRequestOpFrame::getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
                                              int32_t ledgerVersion) const
{
    std::vector<AccountType> allowedAccountTypes = {AccountType::GENERAL, AccountType::NOT_VERIFIED, AccountType::EXCHANGE,
                                                    AccountType::ACCREDITED_INVESTOR, AccountType::INSTITUTIONAL_INVESTOR,
                                                    AccountType::VERIFIED};

	return SourceDetails(allowedAccountTypes, mSourceAccount->getMediumThreshold(),
                         static_cast<int32_t>(SignerType::INVOICE_MANAGER),
                         static_cast<int32_t>(BlockReasons::KYC_UPDATE) |
                         static_cast<int32_t>(BlockReasons::TOO_MANY_KYC_UPDATE_REQUESTS));
}

ManageInvoiceRequestOpFrame::ManageInvoiceRequestOpFrame(Operation const& op, OperationResult& res,
                                     TransactionFrame& parentTx)
    : OperationFrame(op, res, parentTx)
    , mManageInvoiceRequest(mOperation.body.manageInvoiceRequestOp())
{
}

std::string
ManageInvoiceRequestOpFrame::getManageInvoiceRequestReference(longstring const& details) const
{
    const auto hash = sha256(xdr::xdr_to_opaque(ReviewableRequestType::INVOICE, details));
    return binToHex(hash);
}

bool
ManageInvoiceRequestOpFrame::doApply(Application& app, LedgerDelta& delta, LedgerManager& ledgerManager)
{
    Database& db = ledgerManager.getDatabase();
	innerResult().code(ManageInvoiceRequestResultCode::SUCCESS);

	if (mManageInvoiceRequest.details.action() == ManageInvoiceRequestAction::CREATE)
	{
	    return createManageInvoiceRequest(app, delta, ledgerManager);
	}

    auto reviewableRequestHelper = ReviewableRequestHelper::Instance();
    auto reviewableRequest = reviewableRequestHelper->loadRequest(mManageInvoiceRequest.details.requestID(), db);
	if (!reviewableRequest || reviewableRequest->getRequestType() != ReviewableRequestType::INVOICE)
	{
	    innerResult().code(ManageInvoiceRequestResultCode::NOT_FOUND);
	    return false;
	}

	if (!(reviewableRequest->getRequestor() == getSourceID()))
	{
        innerResult().code(ManageInvoiceRequestResultCode::NOT_ALLOWED_TO_REMOVE);
        return false;
	}

    LedgerKey requestKey;
    requestKey.type(LedgerEntryType::REVIEWABLE_REQUEST);
    requestKey.reviewableRequest().requestID = mManageInvoiceRequest.details.requestID();
	reviewableRequestHelper->storeDelete(delta, db, requestKey);

	innerResult().success().details.action(ManageInvoiceRequestAction::REMOVE);

	return true;
}

bool
ManageInvoiceRequestOpFrame::createManageInvoiceRequest(Application& app, LedgerDelta& delta,
                                                        LedgerManager& ledgerManager)
{
    Database& db = ledgerManager.getDatabase();
    auto& invoiceRequest = mManageInvoiceRequest.details.invoiceRequest();

    auto senderBalance = BalanceHelper::Instance()->loadBalance(invoiceRequest.sender,
                                                                invoiceRequest.asset, db, &delta);
    if (!senderBalance)
    {
        app.getMetrics().NewMeter({ "op-manage-invoice", "invalid", "sender-balance-not-found" },
                                  "operation").Mark();
        innerResult().code(ManageInvoiceRequestResultCode::BALANCE_NOT_FOUND);
        return false;
    }

    if (!checkMaxInvoicesForReceiverAccount(app, db))
        return false;

    auto reference = getManageInvoiceRequestReference(invoiceRequest.details);

    auto reviewableRequestHelper = ReviewableRequestHelper::Instance();
    if (reviewableRequestHelper->isReferenceExist(db, getSourceID(), reference))
    {
        innerResult().code(ManageInvoiceRequestResultCode::INVOICE_REQUEST_REFERENCE_DUPLICATION);
        return false;
    }

    ReviewableRequestEntry::_body_t body;
    body.type(ReviewableRequestType::INVOICE);
    body.invoiceRequest() = invoiceRequest;

    const auto referencePtr = xdr::pointer<string64>(new string64(reference));
    auto request = ReviewableRequestFrame::createNewWithHash(delta, getSourceID(), invoiceRequest.sender,
                                                             referencePtr, body, ledgerManager.getCloseTime());

    EntryHelperProvider::storeAddEntry(delta, db, request->mEntry);

    auto receiverBalanceID = AccountManager::loadOrCreateBalanceForAsset(getSourceID(),
                                                                         invoiceRequest.asset, db, delta);

    innerResult().success().details.action(ManageInvoiceRequestAction::CREATE);
    innerResult().success().details.response().requestID = request->getRequestID();
    innerResult().success().details.response().receiverBalance = receiverBalanceID;
    innerResult().success().details.response().senderBalance = senderBalance->getBalanceID();

    return true;
}

bool
ManageInvoiceRequestOpFrame::checkMaxInvoicesForReceiverAccount(Application& app, Database& db)
{
    auto reviewableRequestHelper = ReviewableRequestHelper::Instance();
    auto allRequests = reviewableRequestHelper->loadRequests(getSourceID(), ReviewableRequestType::INVOICE, db);
    if (allRequests.size() >= app.getMaxInvoicesForReceiverAccount())
    {
        app.getMetrics().NewMeter({"op-manage-invoice", "invalid", "too-many-invoices"},
                                  "operation").Mark();
        innerResult().code(ManageInvoiceRequestResultCode::TOO_MANY_INVOICES);
        return false;
    }

    return true;
}

bool
ManageInvoiceRequestOpFrame::doCheckValid(Application& app)
{
    if (mManageInvoiceRequest.details.action() == ManageInvoiceRequestAction::CREATE &&
        mManageInvoiceRequest.details.invoiceRequest().amount == 0)
    {
        app.getMetrics().NewMeter({"op-manage-invoice", "invalid", "malformed-zero-amount"},
                         "operation").Mark();
        innerResult().code(ManageInvoiceRequestResultCode::MALFORMED);
        return false;
    }

    return true;
}

}
