#include <ledger/ReviewableRequestFrame.h>
#include "transactions/ManageInvoiceRequestOpFrame.h"
#include "database/Database.h"
#include "main/Application.h"
#include "medida/meter.h"
#include <crypto/SHA.h>
#include <ledger/ReviewableRequestHelper.h>
#include <ledger/ContractHelper.h>
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
                                     AccountType::VERIFIED, AccountType::MASTER}, true, true)}
	};
}

SourceDetails
ManageInvoiceRequestOpFrame::getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
                                              int32_t ledgerVersion) const
{
    std::vector<AccountType> allowedAccountTypes = {AccountType::GENERAL, AccountType::NOT_VERIFIED, AccountType::EXCHANGE,
                                                    AccountType::ACCREDITED_INVESTOR, AccountType::INSTITUTIONAL_INVESTOR,
                                                    AccountType::VERIFIED, AccountType::MASTER};

	return SourceDetails(allowedAccountTypes, mSourceAccount->getMediumThreshold(),
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

    auto senderBalance = BalanceHelper::Instance()->loadBalance(invoiceCreationRequest.sender,
                                                                invoiceCreationRequest.asset, db, &delta);
    if (!senderBalance)
    {
        innerResult().code(ManageInvoiceRequestResultCode::BALANCE_NOT_FOUND);
        return false;
    }

    if (!checkMaxInvoicesForReceiverAccount(app, db))
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
ManageInvoiceRequestOpFrame::checkMaxInvoicesForReceiverAccount(Application& app, Database& db)
{
    auto reviewableRequestHelper = ReviewableRequestHelper::Instance();
    auto allRequests = reviewableRequestHelper->loadRequests(getSourceID(), ReviewableRequestType::INVOICE, db);
    if (allRequests.size() >= app.getMaxInvoicesForReceiverAccount())
    {
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
