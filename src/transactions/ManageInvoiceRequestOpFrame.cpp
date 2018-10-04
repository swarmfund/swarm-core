#include <ledger/ReviewableRequestFrame.h>
#include "transactions/ManageInvoiceRequestOpFrame.h"
#include "database/Database.h"
#include "main/Application.h"
#include "medida/meter.h"
#include <crypto/SHA.h>
#include <ledger/ReviewableRequestHelper.h>
#include <ledger/ContractHelper.h>
#include <ledger/KeyValueHelperLegacy.h>
#include "medida/metrics_registry.h"
#include "ledger/LedgerDelta.h"
#include "ledger/BalanceHelperLegacy.h"
#include "ManageKeyValueOpFrame.h"

namespace stellar
{
using xdr::operator==;


std::unordered_map<AccountID, CounterpartyDetails>
ManageInvoiceRequestOpFrame::getCounterpartyDetails(Database & db, LedgerDelta * delta) const
{
    if (mManageInvoiceRequest.details.action() == ManageInvoiceRequestAction::REMOVE) {
        // no counterparties
        return{};
    }
    return{
            {mManageInvoiceRequest.details.invoiceRequest().sender,
                    CounterpartyDetails(getAllAccountTypes(), true, true)},
    };
}

SourceDetails
ManageInvoiceRequestOpFrame::getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
                                              int32_t ledgerVersion) const
{
	return SourceDetails(getAllAccountTypes(), mSourceAccount->getHighThreshold(),
                         static_cast<int32_t>(SignerType::INVOICE_MANAGER));
}

ManageInvoiceRequestOpFrame::ManageInvoiceRequestOpFrame(Operation const& op, OperationResult& res,
                                     TransactionFrame& parentTx)
    : OperationFrame(op, res, parentTx)
    , mManageInvoiceRequest(mOperation.body.manageInvoiceRequestOp())
{
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

	auto invoiceRequest = reviewableRequest->getRequestEntry().body.invoiceRequest();

	if (invoiceRequest.isApproved)
	{
	    innerResult().code(ManageInvoiceRequestResultCode::INVOICE_IS_APPROVED);
	    return false;
	}

	if (!!invoiceRequest.contractID)
	{
	    auto contractHelper = ContractHelper::Instance();
	    auto contractFrame = contractHelper->loadContract(*invoiceRequest.contractID, db, &delta);

	    if (!contractFrame)
	    {
	        innerResult().code(ManageInvoiceRequestResultCode::CONTRACT_NOT_FOUND);
            return false;
	    }

	    auto requestID = reviewableRequest->getRequestID();
	    auto& invoices = contractFrame->getInvoiceRequestIDs();

        auto invoicePos = std::find(invoices.begin(), invoices.end(), requestID);
	    if (invoicePos == invoices.end())
	    {
            CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected contract state. "
                                                   << "Expected invoice to be attached to contract. "
                                                   << "contractID: " + std::to_string(*invoiceRequest.contractID)
                                                   << "invoice requestID: " +
                                                      std::to_string(requestID);
            throw std::runtime_error("Unexpected contract state. Expected invoice to be attached to contract.");
	    }

	    invoices.erase(invoicePos);
	    contractHelper->storeChange(delta, db, contractFrame->mEntry);
	}

	reviewableRequestHelper->storeDelete(delta, db, reviewableRequest->getKey());

	innerResult().success().details.action(ManageInvoiceRequestAction::REMOVE);

	return true;
}

bool
ManageInvoiceRequestOpFrame::createManageInvoiceRequest(Application& app, LedgerDelta& delta,
                                                        LedgerManager& ledgerManager)
{
    Database& db = ledgerManager.getDatabase();
    auto& invoiceCreationRequest = mManageInvoiceRequest.details.invoiceRequest();

    auto senderBalance = BalanceHelperLegacy::Instance()->loadBalance(invoiceCreationRequest.sender,
                                                                invoiceCreationRequest.asset, db, &delta);
    if (!senderBalance)
    {
        innerResult().code(ManageInvoiceRequestResultCode::BALANCE_NOT_FOUND);
        return false;
    }

    if (!checkMaxInvoicesForReceiverAccount(app, db, delta))
        return false;

    if (!checkMaxInvoiceDetailsLength(app, db, delta))
        return false;

    auto receiverBalanceID = AccountManager::loadOrCreateBalanceForAsset(getSourceID(),
                                                                         invoiceCreationRequest.asset, db, delta);
    InvoiceRequest invoiceRequest;
    invoiceRequest.asset = invoiceCreationRequest.asset;
    invoiceRequest.amount = invoiceCreationRequest.amount;
    invoiceRequest.details = invoiceCreationRequest.details;
    invoiceRequest.isApproved = false;
    invoiceRequest.contractID = invoiceCreationRequest.contractID;
    invoiceRequest.senderBalance = senderBalance->getBalanceID();
    invoiceRequest.receiverBalance = receiverBalanceID;
    invoiceRequest.ext.v(LedgerVersion::EMPTY_VERSION);

    ReviewableRequestEntry::_body_t body;
    body.type(ReviewableRequestType::INVOICE);
    body.invoiceRequest() = invoiceRequest;

    auto request = ReviewableRequestFrame::createNewWithHash(delta, getSourceID(), invoiceCreationRequest.sender,
                                                             nullptr, body, ledgerManager.getCloseTime());

    EntryHelperProvider::storeAddEntry(delta, db, request->mEntry);

    if (invoiceCreationRequest.contractID)
    {
        auto contractHelper = ContractHelper::Instance();
        auto contractFrame = contractHelper->loadContract(*invoiceCreationRequest.contractID, db, &delta);

        if (!contractFrame)
        {
            innerResult().code(ManageInvoiceRequestResultCode::CONTRACT_NOT_FOUND);
            return false;
        }

        if (!(contractFrame->getContractor() == getSourceID()))
        {
            innerResult().code(ManageInvoiceRequestResultCode::ONLY_CONTRACTOR_CAN_ATTACH_INVOICE_TO_CONTRACT);
            return false;
        }

        if (!(contractFrame->getCustomer() == invoiceCreationRequest.sender))
        {
            innerResult().code(ManageInvoiceRequestResultCode::SENDER_ACCOUNT_MISMATCHED);
            return false;
        }

        contractFrame->addInvoice(request->getRequestID());
        contractHelper->storeChange(delta, db, contractFrame->mEntry);
    }

    innerResult().success().details.action(ManageInvoiceRequestAction::CREATE);
    innerResult().success().details.response().requestID = request->getRequestID();
    innerResult().success().details.response().receiverBalance = receiverBalanceID;
    innerResult().success().details.response().senderBalance = senderBalance->getBalanceID();

    return true;
}

bool
ManageInvoiceRequestOpFrame::checkMaxInvoicesForReceiverAccount(Application& app, Database& db, LedgerDelta& delta)
{
    auto maxInvoicesCount = obtainMaxInvoicesCount(app, db, delta);

    auto reviewableRequestHelper = ReviewableRequestHelper::Instance();
    auto allRequests = reviewableRequestHelper->loadRequests(getSourceID(), ReviewableRequestType::INVOICE, db);
    if (allRequests.size() >= maxInvoicesCount)
    {
        innerResult().code(ManageInvoiceRequestResultCode::TOO_MANY_INVOICES);
        return false;
    }

    return true;
}

bool
ManageInvoiceRequestOpFrame::checkMaxInvoiceDetailsLength(Application& app, Database& db, LedgerDelta& delta)
{
    auto maxInvoiceDetailsLength = obtainMaxInvoiceDetailsLength(app, db, delta);

    auto reviewableRequestHelper = ReviewableRequestHelper::Instance();
    if (mManageInvoiceRequest.details.invoiceRequest().details.size() >= maxInvoiceDetailsLength)
    {
        innerResult().code(ManageInvoiceRequestResultCode::DETAILS_TOO_LONG);
        return false;
    }

    return true;
}

int64_t
ManageInvoiceRequestOpFrame::obtainMaxInvoicesCount(Application& app, Database& db, LedgerDelta& delta)
{
    auto maxInvoicesCountKey = ManageKeyValueOpFrame::makeMaxInvoicesCountKey();
    auto maxInvoicesCountKeyValue = KeyValueHelperLegacy::Instance()->loadKeyValue(maxInvoicesCountKey, db, &delta);

    if (!maxInvoicesCountKeyValue)
    {
        return app.getMaxInvoicesForReceiverAccount();
    }

    if (maxInvoicesCountKeyValue->getKeyValueEntryType() != KeyValueEntryType::UINT32)
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected database state. "
             << "Expected max invoices count key value to be UINT32. Actual: "
             << xdr::xdr_traits<KeyValueEntryType>::enum_name(maxInvoicesCountKeyValue->getKeyValueEntryType());
        throw std::runtime_error("Unexpected database state, expected max invoices count key value to be UINT32");
    }

    return maxInvoicesCountKeyValue->getKeyValue().value.ui32Value();
}

uint64_t
ManageInvoiceRequestOpFrame::obtainMaxInvoiceDetailsLength(Application& app, Database& db, LedgerDelta& delta)
{
    auto maxInvoicesDetailsLengthKey = ManageKeyValueOpFrame::makeMaxInvoiceDetailLengthKey();
    auto maxInvoicesDetailsLengthKeyValue = KeyValueHelperLegacy::Instance()->
            loadKeyValue(maxInvoicesDetailsLengthKey, db, &delta);

    if (!maxInvoicesDetailsLengthKeyValue)
    {
        return app.getMaxInvoiceDetailLength();
    }

    if (maxInvoicesDetailsLengthKeyValue->getKeyValueEntryType() != KeyValueEntryType::UINT32)
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected database state. "
             << "Expected max invoices detail length key value to be UINT32. Actual: "
             << xdr::xdr_traits<KeyValueEntryType>::enum_name(maxInvoicesDetailsLengthKeyValue->getKeyValueEntryType());
        throw std::runtime_error("Unexpected database state, expected max invoices details length key value to be UINT32");
    }

    return maxInvoicesDetailsLengthKeyValue->getKeyValue().value.ui32Value();
}

bool
ManageInvoiceRequestOpFrame::doCheckValid(Application& app)
{
    if (mManageInvoiceRequest.details.action() == ManageInvoiceRequestAction::CREATE &&
        mManageInvoiceRequest.details.invoiceRequest().amount == 0)
    {
        innerResult().code(ManageInvoiceRequestResultCode::MALFORMED);
        return false;
    }

    if (mManageInvoiceRequest.details.action() == ManageInvoiceRequestAction::CREATE &&
        mManageInvoiceRequest.details.invoiceRequest().sender == getSourceID())
    {
        innerResult().code(ManageInvoiceRequestResultCode::MALFORMED);
        return false;
    }

    return true;
}

}
