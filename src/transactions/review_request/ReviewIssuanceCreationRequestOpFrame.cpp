#include <ledger/AccountHelper.h>
#include <transactions/issuance/CreateIssuanceRequestOpFrame.h>
#include <ledger/ReviewableRequestHelper.h>
#include "ledger/LedgerDelta.h"
#include "ledger/AssetHelper.h"
#include "ledger/BalanceHelper.h"
#include "main/Application.h"
#include "xdrpp/printer.h"
#include "ReviewRequestHelper.h"

namespace stellar
{

using namespace std;
using xdr::operator==;

bool ReviewIssuanceCreationRequestOpFrame::handleApproveV1(Application &app, LedgerDelta &delta,
														   LedgerManager &ledgerManager,
														   ReviewableRequestFrame::pointer request)
{
    request->checkRequestType(ReviewableRequestType::ISSUANCE_CREATE);

	auto& issuanceRequest = request->getRequestEntry().body.issuanceRequest();
	Database& db = ledgerManager.getDatabase();
	createReference(delta, db, request->getRequestor(), request->getReference());

	auto asset = AssetHelper::Instance()->mustLoadAsset(issuanceRequest.asset, db, &delta);

	if (asset->willExceedMaxIssuanceAmount(issuanceRequest.amount)) {
		innerResult().code(ReviewRequestResultCode::MAX_ISSUANCE_AMOUNT_EXCEEDED);
		return false;
	}

	if (!asset->isAvailableForIssuanceAmountSufficient(issuanceRequest.amount)) {
		innerResult().code(ReviewRequestResultCode::INSUFFICIENT_AVAILABLE_FOR_ISSUANCE_AMOUNT);
		return false;
	}

	if (!asset->tryIssue(issuanceRequest.amount)) {
		CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected state. Failed to fulfill request: "
                                               << xdr::xdr_to_string(request->getRequestEntry());
		throw std::runtime_error("Unexpected issuance result. Expected to be able to issue");
	}

	EntryHelperProvider::storeChangeEntry(delta, db, asset->mEntry);

	auto receiver = BalanceHelper::Instance()->mustLoadBalance(issuanceRequest.receiver, db, &delta);

	uint64_t totalFee = 0;
	if (!safeSum(issuanceRequest.fee.fixed, issuanceRequest.fee.percent, totalFee)) {
		CLOG(ERROR, Logging::OPERATION_LOGGER) << "totalFee overflows uint64 for request: " << request->getRequestID();
		throw std::runtime_error("totalFee overflows uint64");
	}

	if (totalFee >= issuanceRequest.amount) {
		CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected state. totalFee exceeds amount for request: "
											   << request->getRequestID();
		throw std::runtime_error("Unexpected state. totalFee exceeds amount");
	}

	auto receiverAccount = AccountHelper::Instance()->mustLoadAccount(receiver->getAccountID(), db);
	if (AccountManager::isAllowedToReceive(receiver->getBalanceID(), db) != AccountManager::SUCCESS) {
		CLOG(ERROR, Logging::OPERATION_LOGGER) << "Asset requires receiver account to have KYC or be VERIFIED "
											   << request->getRequestID();
		throw std::runtime_error("Unexpected state. Asset requires KYC or VERIFIED but account is NOT_VERIFIED");
	}

	//transfer fee
	AccountManager accountManager(app, db, delta, ledgerManager);
	accountManager.transferFee(issuanceRequest.asset, totalFee);

	uint64_t destinationReceive = issuanceRequest.amount - totalFee;
	if (!receiver->tryFundAccount(destinationReceive)) {
		innerResult().code(ReviewRequestResultCode::FULL_LINE);
		return false;
	}

	EntryHelperProvider::storeChangeEntry(delta, db, receiver->mEntry);

	EntryHelperProvider::storeDeleteEntry(delta, db, request->getKey());
	innerResult().code(ReviewRequestResultCode::SUCCESS);
	return true;
}

bool ReviewIssuanceCreationRequestOpFrame::handleApproveV2(Application &app, LedgerDelta &delta,
														   LedgerManager &ledgerManager,
														   ReviewableRequestFrame::pointer request)
{
	request->checkRequestType(ReviewableRequestType::ISSUANCE_CREATE);

	auto& issuanceRequest = request->getRequestEntry().body.issuanceRequest();
	Database& db = ledgerManager.getDatabase();

	auto asset = AssetHelper::Instance()->mustLoadAsset(issuanceRequest.asset, db, &delta);
	if (asset->willExceedMaxIssuanceAmount(issuanceRequest.amount))
	{
		innerResult().code(ReviewRequestResultCode::MAX_ISSUANCE_AMOUNT_EXCEEDED);
		return false;
	}

	auto& requestEntry = request->getRequestEntry();

	requestEntry.ext.tasksExt().pendingTasks &= ~CreateIssuanceRequestOpFrame::ISSUANCE_MANUAL_REVIEW_REQUIRED;

	if (!asset->isAvailableForIssuanceAmountSufficient(issuanceRequest.amount))
	{
		requestEntry.ext.tasksExt().allTasks |= CreateIssuanceRequestOpFrame::INSUFFICIENT_AVAILABLE_FOR_ISSUANCE_AMOUNT;
		requestEntry.ext.tasksExt().pendingTasks |= CreateIssuanceRequestOpFrame::INSUFFICIENT_AVAILABLE_FOR_ISSUANCE_AMOUNT;
		EntryHelperProvider::storeChangeEntry(delta, db, request->mEntry);
		innerResult().code(ReviewRequestResultCode::SUCCESS);
		innerResult().success().ext.v(LedgerVersion::ADD_TASKS_TO_REVIEWABLE_REQUEST);
		innerResult().success().ext.extendedResult().fulfilled = false;
		return true;
	}

	requestEntry.ext.tasksExt().pendingTasks &= ~CreateIssuanceRequestOpFrame::INSUFFICIENT_AVAILABLE_FOR_ISSUANCE_AMOUNT;

	requestEntry.ext.tasksExt().allTasks |= mReviewRequest.ext.reviewDetails().tasksToAdd;
	requestEntry.ext.tasksExt().pendingTasks &= ~mReviewRequest.ext.reviewDetails().tasksToRemove;
	requestEntry.ext.tasksExt().pendingTasks |= mReviewRequest.ext.reviewDetails().tasksToAdd;
	requestEntry.ext.tasksExt().externalDetails.emplace_back(mReviewRequest.ext.reviewDetails().externalDetails);

	ReviewableRequestHelper::Instance()->storeChange(delta, db, request->mEntry);

	if (!request->canBeFulfilled(ledgerManager))
	{
		innerResult().code(ReviewRequestResultCode::SUCCESS);
        innerResult().success().ext.v(LedgerVersion::ADD_TASKS_TO_REVIEWABLE_REQUEST);
        innerResult().success().ext.extendedResult().fulfilled = false;
		return true;
	}

    createReference(delta, db, request->getRequestor(), request->getReference());
	EntryHelperProvider::storeDeleteEntry(delta, db, request->getKey());

	if (!asset->tryIssue(issuanceRequest.amount))
	{
		CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected state. Failed to fulfill request: "
											   << xdr::xdr_to_string(request->getRequestEntry());
		throw std::runtime_error("Unexpected issuance result. Expected to be able to issue");
	}

	EntryHelperProvider::storeChangeEntry(delta, db, asset->mEntry);

	auto receiver = BalanceHelper::Instance()->mustLoadBalance(issuanceRequest.receiver, db, &delta);

	uint64_t totalFee = 0;
	if (!safeSum(issuanceRequest.fee.fixed, issuanceRequest.fee.percent, totalFee))
	{
		CLOG(ERROR, Logging::OPERATION_LOGGER) << "totalFee overflows uint64 for request: " << request->getRequestID();
		throw std::runtime_error("totalFee overflows uint64");
	}

	if (totalFee >= issuanceRequest.amount)
	{
		CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected state. totalFee exceeds amount for request: "
											   << request->getRequestID();
		throw std::runtime_error("Unexpected state. totalFee exceeds amount");
	}

	auto receiverAccount = AccountHelper::Instance()->mustLoadAccount(receiver->getAccountID(), db);
	if (AccountManager::isAllowedToReceive(receiver->getBalanceID(), db) != AccountManager::SUCCESS)
	{
		CLOG(ERROR, Logging::OPERATION_LOGGER) << "Asset requires receiver account to have KYC or be VERIFIED "
											   << request->getRequestID();
		throw std::runtime_error("Unexpected state. Asset requires KYC or VERIFIED but account is NOT_VERIFIED");
	}

	//transfer fee
	AccountManager accountManager(app, db, delta, ledgerManager);
	accountManager.transferFee(issuanceRequest.asset, totalFee);

	uint64_t destinationReceive = issuanceRequest.amount - totalFee;
	if (!receiver->tryFundAccount(destinationReceive))
	{
		innerResult().code(ReviewRequestResultCode::FULL_LINE);
		return false;
	}

	EntryHelperProvider::storeChangeEntry(delta, db, receiver->mEntry);
	innerResult().code(ReviewRequestResultCode::SUCCESS);
    innerResult().success().ext.v(LedgerVersion::ADD_TASKS_TO_REVIEWABLE_REQUEST);
    innerResult().success().ext.extendedResult().fulfilled = true;
	return true;
}


bool ReviewIssuanceCreationRequestOpFrame::handleApprove(Application & app, LedgerDelta & delta,
                                                         LedgerManager & ledgerManager,
                                                         ReviewableRequestFrame::pointer request)
{
	if (ledgerManager.shouldUse(LedgerVersion::ADD_TASKS_TO_REVIEWABLE_REQUEST) &&
        mReviewRequest.ext.v() == LedgerVersion::ADD_TASKS_TO_REVIEWABLE_REQUEST &&
        request->getRequestEntry().ext.v() == LedgerVersion::ADD_TASKS_TO_REVIEWABLE_REQUEST)
    {
        return handleApproveV2(app, delta, ledgerManager, request);
    }
	return handleApproveV1(app, delta, ledgerManager, request);
}

bool ReviewIssuanceCreationRequestOpFrame::handleReject(Application & app, LedgerDelta & delta, LedgerManager & ledgerManager,
                                                        ReviewableRequestFrame::pointer request)
{
	innerResult().code(ReviewRequestResultCode::REJECT_NOT_ALLOWED);
	return false;
}

SourceDetails ReviewIssuanceCreationRequestOpFrame::getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
                                                                            int32_t ledgerVersion) const
{
    auto allowedSigners = static_cast<int32_t>(SignerType::ASSET_MANAGER);

    auto newSingersVersion = static_cast<int32_t>(LedgerVersion::NEW_SIGNER_TYPES);
    if (ledgerVersion >= newSingersVersion)
    {
        allowedSigners = static_cast<int32_t>(SignerType::USER_ISSUANCE_MANAGER);
    }

	if (ledgerVersion >= static_cast<int32_t>(LedgerVersion::ADD_TASKS_TO_REVIEWABLE_REQUEST))
	{
		return SourceDetails({AccountType::MASTER, AccountType::SYNDICATE}, mSourceAccount->getHighThreshold(),
							 allowedSigners);
	}

	return SourceDetails({AccountType::MASTER}, mSourceAccount->getHighThreshold(), allowedSigners);
}

ReviewIssuanceCreationRequestOpFrame::ReviewIssuanceCreationRequestOpFrame(Operation const & op, OperationResult & res,
                                                                           TransactionFrame & parentTx) :
	ReviewRequestOpFrame(op, res, parentTx)
{
}

bool ReviewIssuanceCreationRequestOpFrame::doCheckValid(Application &app)
{
    if (!app.getLedgerManager().shouldUse(LedgerVersion::ADD_TASKS_TO_REVIEWABLE_REQUEST) ||
        mReviewRequest.ext.v() != LedgerVersion::ADD_TASKS_TO_REVIEWABLE_REQUEST)
    {
        return true;
    }

	int32_t systemTasks = CreateIssuanceRequestOpFrame::INSUFFICIENT_AVAILABLE_FOR_ISSUANCE_AMOUNT |
						  CreateIssuanceRequestOpFrame::ISSUANCE_MANUAL_REVIEW_REQUIRED;

	if ((mReviewRequest.ext.reviewDetails().tasksToAdd & systemTasks) != 0 ||
        (mReviewRequest.ext.reviewDetails().tasksToRemove & systemTasks) != 0)
	{
		innerResult().code(ReviewRequestResultCode::SYSTEM_TASKS_NOT_ALLOWED);
		return false;
	}

	if (!isValidJson(mReviewRequest.ext.reviewDetails().externalDetails))
	{
		innerResult().code(ReviewRequestResultCode::INVALID_EXTERNAL_DETAILS);
		return false;
	}

    return true;
}

}
