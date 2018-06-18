// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "util/asio.h"
#include "OperationFrame.h"
#include "main/Application.h"
#include "xdrpp/marshal.h"
#include <string>
#include "util/Logging.h"
#include "ledger/LedgerDelta.h"
#include "ledger/FeeFrame.h"
#include "ledger/PaymentRequestFrame.h"
#include "ledger/AccountTypeLimitsFrame.h"
#include "ledger/ReferenceFrame.h"
#include "ledger/AccountHelper.h"
#include "transactions/TransactionFrame.h"
#include "transactions/CreateAccountOpFrame.h"
#include "transactions/payment/PaymentOpFrame.h"
#include "transactions/payment/PaymentOpV2Frame.h"
#include "transactions/SetOptionsOpFrame.h"
#include "transactions/SetFeesOpFrame.h"
#include "transactions/ManageAccountOpFrame.h"
#include "transactions/ManageBalanceOpFrame.h"
#include "transactions/CreateWithdrawalRequestOpFrame.h"
#include "transactions/review_request/ReviewPaymentRequestOpFrame.h"
#include "transactions/manage_asset/ManageAssetOpFrame.h"
#include "transactions/issuance/CreatePreIssuanceRequestOpFrame.h"
#include "transactions/issuance/CreateIssuanceRequestOpFrame.h"
#include "transactions/ManageLimitsOpFrame.h"
#include "transactions/ManageAssetPairOpFrame.h"
#include "transactions/DirectDebitOpFrame.h"
#include "transactions/ManageInvoiceOpFrame.h"
#include "transactions/review_request/ReviewRequestOpFrame.h"
#include "transactions/CreateSaleCreationRequestOpFrame.h"
#include "transactions/manage_external_system_account_id_pool/ManageExternalSystemAccountIDPoolEntryOpFrame.h"
#include "transactions/CreateAMLAlertRequestOpFrame.h"
#include "transactions/kyc/CreateKYCReviewableRequestOpFrame.h"
#include "transactions/ManageSaleOpFrame.h"
#include "database/Database.h"

#include "medida/meter.h"
#include "medida/metrics_registry.h"
#include "dex/ManageOfferOpFrame.h"
#include "CheckSaleStateOpFrame.h"
#include "BindExternalSystemAccountIdOpFrame.h"
#include "ManageKeyValueOpFrame.h"
#include "CreateManageLimitsRequestOpFrame.h"

namespace stellar
{

using namespace std;

    
shared_ptr<OperationFrame>
OperationFrame::makeHelper(Operation const& op, OperationResult& res,
                           TransactionFrame& tx)
{
    switch (op.body.type())
    {
    case OperationType::CREATE_ACCOUNT:
        return shared_ptr<OperationFrame>(new CreateAccountOpFrame(op, res, tx));
    case OperationType::PAYMENT:
        return shared_ptr<OperationFrame>(new PaymentOpFrame(op, res, tx));
    case OperationType::SET_OPTIONS:
        return shared_ptr<OperationFrame>(new SetOptionsOpFrame(op, res, tx));
    case OperationType::CREATE_ISSUANCE_REQUEST:
		return shared_ptr<OperationFrame>(new CreateIssuanceRequestOpFrame(op, res, tx));
    case OperationType::SET_FEES:
        return shared_ptr<OperationFrame>(new SetFeesOpFrame(op, res, tx));
	case OperationType::MANAGE_ACCOUNT:
		return shared_ptr<OperationFrame>(new ManageAccountOpFrame(op, res, tx));
    case OperationType::CREATE_WITHDRAWAL_REQUEST:
		return shared_ptr<OperationFrame>(new CreateWithdrawalRequestOpFrame(op, res, tx));
    case OperationType::MANAGE_BALANCE:
		return shared_ptr<OperationFrame>(new ManageBalanceOpFrame(op, res, tx));
    case OperationType::REVIEW_PAYMENT_REQUEST:
		return shared_ptr<OperationFrame>(new ReviewPaymentRequestOpFrame(op, res, tx));
    case OperationType::MANAGE_ASSET:
		return shared_ptr<OperationFrame>(ManageAssetOpFrame::makeHelper(op, res, tx));
    case OperationType::CREATE_PREISSUANCE_REQUEST:
		return shared_ptr<OperationFrame>(new CreatePreIssuanceRequestOpFrame(op, res, tx));
    case OperationType::MANAGE_LIMITS:
		return shared_ptr<OperationFrame>(new ManageLimitsOpFrame(op, res, tx));
	case OperationType::MANAGE_ASSET_PAIR:
		return shared_ptr<OperationFrame>(new ManageAssetPairOpFrame(op, res, tx));
    case OperationType::DIRECT_DEBIT:
        return shared_ptr<OperationFrame>(new DirectDebitOpFrame(op, res, tx));
	case OperationType::MANAGE_OFFER:
		return shared_ptr<OperationFrame>(ManageOfferOpFrame::make(op, res, tx));
    case OperationType::MANAGE_INVOICE:
        return shared_ptr<OperationFrame>(new ManageInvoiceOpFrame(op, res, tx));
    case OperationType::REVIEW_REQUEST:
		return shared_ptr<OperationFrame>(ReviewRequestOpFrame::makeHelper(op, res, tx));
    case OperationType::CREATE_SALE_REQUEST:
        return shared_ptr<OperationFrame>(new CreateSaleCreationRequestOpFrame(op, res, tx));
    case OperationType::CHECK_SALE_STATE:
        return shared_ptr<OperationFrame>(new CheckSaleStateOpFrame(op, res, tx));
    case OperationType::MANAGE_EXTERNAL_SYSTEM_ACCOUNT_ID_POOL_ENTRY:
        return shared_ptr<OperationFrame>(ManageExternalSystemAccountIdPoolEntryOpFrame::makeHelper(op, res, tx));
    case OperationType::BIND_EXTERNAL_SYSTEM_ACCOUNT_ID:
        return shared_ptr<OperationFrame>(new BindExternalSystemAccountIdOpFrame(op, res, tx));
    case OperationType::CREATE_AML_ALERT:
        return shared_ptr<OperationFrame>(new CreateAMLAlertRequestOpFrame(op,res,tx));
	case OperationType::CREATE_KYC_REQUEST:
		return shared_ptr<OperationFrame>(new CreateUpdateKYCRequestOpFrame(op, res, tx));
    case OperationType::PAYMENT_V2:
        return shared_ptr<OperationFrame>(new PaymentOpV2Frame(op, res, tx));
    case OperationType::MANAGE_KEY_VALUE:
        return shared_ptr<OperationFrame>(new ManageKeyValueOpFrame(op, res, tx));
    case OperationType::MANAGE_SALE:
        return shared_ptr<OperationFrame>(new ManageSaleOpFrame(op, res, tx));
    case OperationType::CREATE_MANAGE_LIMITS_REQUEST:
        return shared_ptr<OperationFrame>(new CreateManageLimitsRequestOpFrame(op, res, tx));
    default:
        ostringstream err;
        err << "Unknown Tx type: " << static_cast<int32_t >(op.body.type());
        throw std::invalid_argument(err.str());
    }
}

OperationFrame::OperationFrame(Operation const& op, OperationResult& res,
                               TransactionFrame& parentTx)
    : mOperation(op), mParentTx(parentTx), mResult(res)
{
}

bool
OperationFrame::apply(LedgerDelta& delta, Application& app)
{
    bool res;
    res = checkValid(app, &delta);
    if (!res)
        return res;
    bool isApplied = doApply(app, delta, app.getLedgerManager());
	app.getMetrics().NewMeter({ "operation", isApplied ? "applied" : "rejected", getInnerResultCodeAsStr() }, "operation").Mark();
	return isApplied;
}

std::string OperationFrame::getInnerResultCodeAsStr() {
	// Default implementation does nothing, make remove this implementation when all operations switched to it
	return "not_implemented";
}

std::unordered_map<AccountID, CounterpartyDetails>
OperationFrame::getCounterpartyDetails(Database &db, LedgerDelta *delta, int32_t ledgerVersion) const {
    return getCounterpartyDetails(db, delta);
}

SourceDetails OperationFrame::getSourceAccountDetails(
    std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
    int32_t ledgerVersion, Database& db) const
{
    return getSourceAccountDetails(counterpartiesDetails, ledgerVersion);
}

bool OperationFrame::isAllowed() const
{
	// by default all operations are allowed
	return true;
}

int64_t OperationFrame::getPaidFee() const {
	// default fee for all operations is 0, finantial operations must override this function
    return 0;
}
    
bool
OperationFrame::doCheckSignature(Application& app, Database& db, SourceDetails& sourceDetails)
{
    auto result = mParentTx.getSignatureValidator()->check(app, db, *mSourceAccount, sourceDetails);
	switch (result)
	{
	case SignatureValidator::Result::SUCCESS:
		return true;
	case SignatureValidator::Result::INVALID_ACCOUNT_TYPE:
		app.getMetrics().NewMeter({ "transaction", "invalid", "not-allowed" }, "transaction").Mark();
		mResult.code(OperationResultCode::opNOT_ALLOWED);
		return false;
	case SignatureValidator::Result::NOT_ENOUGH_WEIGHT:
		app.getMetrics().NewMeter({ "transaction", "invalid", "bad-auth" }, "transaction").Mark();
		mResult.code(OperationResultCode::opBAD_AUTH);
		return false;
	case SignatureValidator::Result::INVALID_SIGNER_TYPE:
		app.getMetrics().NewMeter({ "transaction", "invalid", "invalid-signer-type" }, "transaction").Mark();
		mResult.code(OperationResultCode::opNOT_ALLOWED);
		return false;
	case SignatureValidator::Result::ACCOUNT_BLOCKED:
		app.getMetrics().NewMeter({ "operation", "invalid", "account-is-blocked" }, "operation").Mark();
		mResult.code(OperationResultCode::opACCOUNT_BLOCKED);
		return false;
	case SignatureValidator::Result::EXTRA:
		app.getMetrics().NewMeter({ "operation", "invalid", "bad-auth-extra" }, "operation").Mark();
		mResult.code(OperationResultCode::opBAD_AUTH_EXTRA);
		return false;
	}

	throw runtime_error("Unexpected error code from signatureValidator for operation");
}

AccountID const&
OperationFrame::getSourceID() const
{
    return mOperation.sourceAccount ? *mOperation.sourceAccount
                                    : mParentTx.getEnvelope().tx.sourceAccount;
}


bool
OperationFrame::loadAccount(LedgerDelta* delta, Database& db)
{
    mSourceAccount = mParentTx.loadAccount(delta, db, getSourceID());
    return !!mSourceAccount;
}

PaymentRequestEntry
OperationFrame::createPaymentRequest(uint64 paymentID, BalanceID sourceBalance, int64 sourceSend, int64 sourceSendUniversal,
            BalanceID* destBalance, int64 destReceive, LedgerDelta& delta,
            Database& db, uint64 createdAt, uint64* invoiceID)
{
    LedgerEntry le;
    le.data.type(LedgerEntryType::PAYMENT_REQUEST);
    PaymentRequestEntry& entry = le.data.paymentRequest();

    entry.paymentID = paymentID;
    entry.sourceBalance = sourceBalance;
    entry.sourceSend = sourceSend;
    entry.sourceSendUniversal = sourceSendUniversal;
    if (destBalance)
        entry.destinationBalance.activate() = *destBalance;
    entry.destinationReceive = destReceive;
    entry.createdAt = createdAt;
    if (invoiceID)
        entry.invoiceID.activate() = *invoiceID;
    
    auto paymentRequestFrame = std::make_shared<PaymentRequestFrame>(le);
    EntryHelperProvider::storeAddEntry(delta, db, paymentRequestFrame->mEntry);

    return entry;
}

[[deprecated]]
void
OperationFrame::createReferenceEntry(string reference, LedgerDelta* delta, Database& db)
{
    LedgerEntry le;
    le.data.type(LedgerEntryType::REFERENCE_ENTRY);
    ReferenceEntry& entry = le.data.reference();

    entry.reference = reference;
    auto referenceFrame = std::make_shared<ReferenceFrame>(le);
	EntryHelperProvider::storeAddEntry(*delta, db, referenceFrame->mEntry);
}



OperationResultCode
OperationFrame::getResultCode() const
{
    return mResult.code();
}


// called when determining if we should accept this operation.
// called when determining if we should flood
// make sure sig is correct
// verifies that the operation is well formed (operation specific)
bool
OperationFrame::checkValid(Application& app, LedgerDelta* delta)
{
    if (!isAllowed())
    {
		app.getMetrics().NewMeter({ "operation", "invalid", "not-allowed" }, "operation").Mark();
		mResult.code(OperationResultCode::opNOT_ALLOWED);
		return false;
    }

    bool forApply = (delta != nullptr);
	auto& db = app.getDatabase();
    if (!loadAccount(delta, db))
    {
        if (forApply || !mOperation.sourceAccount)
        {
            app.getMetrics()
                .NewMeter({"operation", "invalid", "no-account"}, "operation")
                .Mark();
            mResult.code(OperationResultCode::opNO_ACCOUNT);
            return false;
        }
        else
        {
            mSourceAccount =
                AccountFrame::makeAuthOnlyAccount(*mOperation.sourceAccount);
        }
    }

	auto counterpartiesDetails = getCounterpartyDetails(db, delta, app.getLedgerManager().getCurrentLedgerHeader().ledgerVersion);
	if (!checkCounterparties(app, counterpartiesDetails))
	{
		return false;
	}

	auto sourceDetails = getSourceAccountDetails(counterpartiesDetails, app.getLedgerManager().getCurrentLedgerHeader().ledgerVersion, db);
    if (!doCheckSignature(app, db, sourceDetails))
    {
        return false;
    }

    if (!forApply)
    {
        // safety: operations should not rely on ledger state as
        // previous operations may change it (can even create the account)
        mSourceAccount.reset();
    }

    mResult.code(OperationResultCode::opINNER);
    mResult.tr().type(mOperation.body.type());
    
    bool isValid = doCheckValid(app);
	if (!isValid) {
		app.getMetrics().NewMeter({ "operation", "rejected", getInnerResultCodeAsStr() }, "operation").Mark();
	}

	return isValid;
}

bool
OperationFrame::checkCounterparties(Application& app, std::unordered_map<AccountID, CounterpartyDetails>& counterparties)
{

	auto& db = app.getDatabase();
    
    for (auto& counterpartyPair : counterparties)
    {
		auto accountHelper = AccountHelper::Instance();
		counterpartyPair.second.mAccount  = accountHelper->loadAccount(counterpartyPair.first, db);
		bool isExists = !!counterpartyPair.second.mAccount;

        if (!isExists)
        {
			if (!counterpartyPair.second.mIsMustExists)
				continue;

            app.getMetrics().NewMeter({ "operation", "invalid", "counterparty-not-found" }, "operation").Mark();
            mResult.code(OperationResultCode::opNO_COUNTERPARTY);
            return false;
        }

        if (!counterpartyPair.second.mIsBlockedAllowed && counterpartyPair.second.mAccount->isBlocked())
        {
            app.getMetrics().NewMeter({ "operation", "invalid", "blocked-counterparty" }, "operation").Mark();
            mResult.code(OperationResultCode::opCOUNTERPARTY_BLOCKED);
            return false;
        }
        
		auto& allowedTypes = counterpartyPair.second.mAllowedAccountTypes;
        if(std::find(allowedTypes.begin(), allowedTypes.end(), counterpartyPair.second.mAccount->getAccountType()) == allowedTypes.end())
        {
            app.getMetrics().NewMeter({ "operation", "invalid", "wrong-counterparty-type" }, "operation").Mark();
            mResult.code(OperationResultCode::opCOUNTERPARTY_WRONG_TYPE);
            return false;
        }

        
    }

    return true;
}


}
