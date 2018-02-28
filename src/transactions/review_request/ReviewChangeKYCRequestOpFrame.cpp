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


		auto updatedAccountID = changeKYCRequest.updatedAccount;
		auto updatedAccountFrame = AccountHelper::Instance()->loadAccount(updatedAccountID, db);
		if (!updatedAccountFrame)
		{
			CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected state. Requestor account not found.";
			throw std::runtime_error("Unexpected state. Updated account not found.");
		}
		

		// set KYC Data
		auto kycHelper = AccountKYCHelper::Instance();
		auto updatedKYC = kycHelper->loadAccountKYC(updatedAccountID,db,&delta);
		if (!updatedKYC) {
			auto updatedKYCAccountFrame = AccountKYCFrame::createNew(updatedAccountID,changeKYCRequest.kycData);
			kycHelper->storeAdd(delta, db, updatedKYCAccountFrame->mEntry);
		}
		else {
			updatedKYC->setKYCData(changeKYCRequest.kycData);
			kycHelper->storeChange(delta, db, updatedKYC->mEntry);
		}

		auto& accountEntry = updatedAccountFrame->getAccount();
		accountEntry.ext.v(LedgerVersion::USE_KYC_LEVEL);
		updatedAccountFrame->setKYCLevel(changeKYCRequest.kycLevel);
		updatedAccountFrame->setAccountType(changeKYCRequest.accountTypeToSet);
		EntryHelperProvider::storeChangeEntry(delta, db, updatedAccountFrame->mEntry);

		innerResult().code(ReviewRequestResultCode::SUCCESS);
		return true;

	}


	SourceDetails
		ReviewChangeKYCRequestOpFrame::getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
															   int32_t ledgerVersion) const {
		return SourceDetails({ AccountType::MASTER }, mSourceAccount->getHighThreshold(), static_cast<int32_t>(SignerType::KYC_ACC_MANAGER));

	}

}