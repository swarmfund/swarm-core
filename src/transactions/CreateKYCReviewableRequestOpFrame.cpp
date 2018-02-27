#include "CreateKYCReviewableRequestOpFrame.h"
#include "database/Database.h"
#include "ledger/AccountHelper.h"
#include "main/Application.h"

#include "bucket/BucketApplicator.h"
#include "xdrpp/printer.h"
#include "ledger/LedgerDelta.h"
#include "review_request/ReviewRequestHelper.h"

namespace stellar {
	using namespace std;
	using xdr::operator==;
	CreateKYCRequestOpFrame::CreateKYCRequestOpFrame(Operation const& op, OperationResult& res,
		TransactionFrame& parentTx) 
		: OperationFrame(op, res, parentTx)
		, mCreateKYCRequest(mOperation.body.createKYCRequestOp())
	{

	}
	bool CreateKYCRequestOpFrame::updateKYCRequest(
		Database& db,LedgerDelta& delta,Application& app) {
		auto requestHelper = ReviewableRequestHelper::Instance();
		auto request = requestHelper->loadRequest(mCreateKYCRequest.requestID,mCreateKYCRequest.changeKYCRequest.updatedAccount,
												  ReviewableRequestType::CHANGE_KYC,db,&delta);
		if(!request){
			innerResult().code(CreateKYCRequestResultCode::REQUEST_NOT_EXIST);
			return false;
		}
		auto& requestEntry = request->getRequestEntry();

		buildRequest(requestEntry);

		request->recalculateHashRejectReason();

		ReviewableRequestHelper::Instance()->storeChange(delta,db,request->mEntry);
		
		innerResult().code(CreateKYCRequestResultCode::SUCCESS);
		innerResult().success().requestID = mCreateKYCRequest.requestID;
		return true;
	}
	bool CreateKYCRequestOpFrame::doApply(Application& app, LedgerDelta& delta,
		LedgerManager& ledgerManager) {
		Database& db = ledgerManager.getDatabase();
		

		if (mCreateKYCRequest.requestID!=0) {
			return updateKYCRequest(db,delta,app);
		}

		auto accountHelper = AccountHelper::Instance();
		auto accountFrame = accountHelper->loadAccount(delta, mCreateKYCRequest.changeKYCRequest.updatedAccount, db);
		if (!accountFrame) {
			innerResult().code(CreateKYCRequestResultCode::UPDATED_ACC_NOT_EXIST);
			return false;
		}

		auto& changeKYCRequest = mCreateKYCRequest.changeKYCRequest;
		uint32 kycLevel = changeKYCRequest.kycLevel;
		auto& account = accountFrame->getAccount();

			if (account.accountType == changeKYCRequest.accountTypeToSet &&
				accountFrame->getKYCLevel() == changeKYCRequest.kycLevel)
			{
				innerResult().code(CreateKYCRequestResultCode::SET_TYPE_THE_SAME);
				return false;
			}

		auto reference = getReference();
		const auto referencePtr = xdr::pointer<string64>(new string64(reference));
		auto requestFrame = ReviewableRequestFrame::createNew(delta, changeKYCRequest.updatedAccount, app.getMasterID(), referencePtr, ledgerManager.getCloseTime());

		auto requestHelper = ReviewableRequestHelper::Instance();
		if (requestHelper->isReferenceExist(db, changeKYCRequest.updatedAccount, reference, requestFrame->getRequestID())) {
			innerResult().code(CreateKYCRequestResultCode::REQUEST_EXIST);	
			return false;
		}
		auto& requestEntry = requestFrame->getRequestEntry();
		requestEntry.body.type(ReviewableRequestType::CHANGE_KYC);
		buildRequest(requestEntry);

		requestFrame->recalculateHashRejectReason();
		
		requestHelper->storeAdd(delta, db, requestFrame->mEntry);

		innerResult().code(CreateKYCRequestResultCode::SUCCESS);
	
		innerResult().success().requestID = requestFrame->getRequestID();
		if (getSourceAccount().getAccountType() == AccountType::MASTER) {
			auto reviewableRequest =requestHelper->loadRequest(requestFrame->getRequestID(), requestFrame->getRequestor(), db, &delta);
			ReviewRequestHelper::tryApproveRequest(mParentTx, app, ledgerManager, delta, reviewableRequest);
			
		}
		return true;
	
	}


	bool CreateKYCRequestOpFrame::doCheckValid(Application& app) {
		return true;
	}


	std::unordered_map<AccountID, CounterpartyDetails> CreateKYCRequestOpFrame::getCounterpartyDetails(Database& db, LedgerDelta* delta) const {
		return { {mCreateKYCRequest.changeKYCRequest.updatedAccount,
			CounterpartyDetails({ AccountType::GENERAL,AccountType::NOT_VERIFIED }, true, true)}
		};
	}

	SourceDetails CreateKYCRequestOpFrame::getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
																   int32_t ledgerVersion) const {
		
		if (getSourceID() == mCreateKYCRequest.changeKYCRequest.updatedAccount) {
			return SourceDetails({AccountType::GENERAL, AccountType::NOT_VERIFIED},
				mSourceAccount->getHighThreshold(), static_cast<int32_t>(SignerType::KYC_ACC_MANAGER));
		}
		return SourceDetails({ AccountType::MASTER }, mSourceAccount->getHighThreshold(),
			static_cast<int32_t>(SignerType::KYC_ACC_MANAGER));
	}


	std::string CreateKYCRequestOpFrame::getReference() const
	{
		const auto hash = sha256(xdr::xdr_to_opaque(ReviewableRequestType::CHANGE_KYC));
		return binToHex(hash);
	}


	void CreateKYCRequestOpFrame::buildRequest(ReviewableRequestEntry& requestEntry) {

		requestEntry.body.changeKYCRequest().accountTypeToSet = mCreateKYCRequest.changeKYCRequest.accountTypeToSet;
		requestEntry.body.changeKYCRequest().updatedAccount = mCreateKYCRequest.changeKYCRequest.updatedAccount;
		requestEntry.body.changeKYCRequest().kycLevel = mCreateKYCRequest.changeKYCRequest.kycLevel;
		requestEntry.body.changeKYCRequest().kycData = mCreateKYCRequest.changeKYCRequest.kycData;
	}
}