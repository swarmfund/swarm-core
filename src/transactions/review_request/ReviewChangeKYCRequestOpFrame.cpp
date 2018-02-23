#include "ReviewChangeKYCRequestOpFrame.h"
#include "ReviewRequestOpFrame.h"
#include "ReviewRequestHelper.h"
#include "ledger/EntryHelper.h"
#include "ledger/AccountHelper.h"
#include "ledger/AccountKYCHelper.h"
namespace stellar {
	ReviewChangeKYCRequestOpFrame::ReviewChangeKYCRequestOpFrame(Operation const& op, OperationResult& res, TransactionFrame& parentTx) :
		ReviewRequestOpFrame(op, res, parentTx)
	{

	}
	bool ReviewChangeKYCRequestOpFrame::handleApprove(Application& app, LedgerDelta& delta, LedgerManager& ledgerManager, ReviewableRequestFrame::pointer request) {
		if (request->getRequestType() != ReviewableRequestType::CHANGE_KYC)
		{
			CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected request type. Expected CHANGE_KYC, but got " << xdr::
				xdr_traits<ReviewableRequestType>::
				enum_name(request->getRequestType());
			throw std::invalid_argument("Unexpected request type for review update KYC request");
		}

		Database& db = ledgerManager.getDatabase();
		EntryHelperProvider::storeDeleteEntry(delta, db, request->getKey());

		auto changeKYCRequest = request->getRequestEntry().body.changeKYCRequest();

		AccountID requestorID = request->getRequestor();
		auto requestorAccount = AccountHelper::Instance()->loadAccount(requestorID, db);
		if (!requestorAccount)
		{
			CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected state. Requestor account not found.";
			throw std::runtime_error("Unexpected state. Requestor not found.");
		}
		auto updatedAccountID = changeKYCRequest.updatedAccount;
		auto updatedAccount = AccountHelper::Instance()->loadAccount(updatedAccountID, db);
		if (!updatedAccount)
		{
			CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected state. Requestor account not found.";
			throw std::runtime_error("Unexpected state. Requestor not found.");
		}
		
		// use KYC level
		if (request->mEntry.ext.v() == LedgerVersion::USE_KYC_LEVEL) {
			updatedAccount->mEntry.data.account().ext.kycLevel() = changeKYCRequest.kycLevel;
		}
		// set KYC Data
		auto kycHelper = AccountKYCHelper::Instance();
		auto updatedKYCAccount = kycHelper->loadAccountKYC(changeKYCRequest.updatedAccount,db,&delta);
		if (!updatedKYCAccount) {
			updatedKYCAccount->setKYCData(changeKYCRequest.kycData);
			updatedKYCAccount->mEntry.data.accountKYC().accountID = changeKYCRequest.updatedAccount;
			kycHelper->storeAdd(delta, db, updatedKYCAccount->mEntry);
		}
		else {
			updatedKYCAccount->setKYCData(changeKYCRequest.kycData);
			kycHelper->storeChange(delta, db, updatedKYCAccount->mEntry);
		}

		updatedAccount->setAccountType(changeKYCRequest.accountTypeToSet);
		EntryHelperProvider::storeChangeEntry(delta, db, updatedAccount->mEntry);

		innerResult().code(ReviewRequestResultCode::SUCCESS);
		return true;

	}


	SourceDetails
		ReviewChangeKYCRequestOpFrame::getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails) const {
		return SourceDetails({ AccountType::MASTER }, mSourceAccount->getHighThreshold(), static_cast<int32_t>(SignerType::KYC_ACC_MANAGER));

	}

}