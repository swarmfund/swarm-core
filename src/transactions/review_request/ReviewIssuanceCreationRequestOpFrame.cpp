#include <ledger/AccountHelper.h>
#include <transactions/issuance/CreateIssuanceRequestOpFrame.h>
#include <ledger/ReviewableRequestHelper.h>
#include <ledger/PendingStatisticsHelper.h>
#include "ledger/LedgerDeltaImpl.h"
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

bool ReviewIssuanceCreationRequestOpFrame::
handleApproveV2(Application &app, LedgerDelta &delta,
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

	auto systemTasksToAdd = getSystemTasksToAdd(app, db, delta, ledgerManager, request);
	int32_t systemTasks = CreateIssuanceRequestOpFrame::INSUFFICIENT_AVAILABLE_FOR_ISSUANCE_AMOUNT |
						  CreateIssuanceRequestOpFrame::ISSUANCE_MANUAL_REVIEW_REQUIRED |
						  CreateIssuanceRequestOpFrame::DEPOSIT_LIMIT_EXCEEDED;
	if ((systemTasksToAdd & ~systemTasks) != 0 ){
		throw std::runtime_error("Expected only system tasks, got more");
	}

    requestEntry.ext.tasksExt().allTasks |= systemTasksToAdd;
	requestEntry.ext.tasksExt().pendingTasks |= systemTasksToAdd;

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
		innerResult().success().ext.extendedResult().typeExt.requestType(ReviewableRequestType::NONE);
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
	innerResult().success().ext.extendedResult().typeExt.requestType(ReviewableRequestType::NONE);
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

	if (ledgerVersion < static_cast<int32_t>(LedgerVersion::ADD_TASKS_TO_REVIEWABLE_REQUEST))
	{
        return SourceDetails({AccountType::MASTER}, mSourceAccount->getHighThreshold(), allowedSigners);
	}

	// TODO: maybe must be refactored
    if (mReviewRequest.ext.v() != LedgerVersion::ADD_TASKS_TO_REVIEWABLE_REQUEST)
    {
        return SourceDetails({AccountType::MASTER, AccountType::SYNDICATE}, mSourceAccount->getHighThreshold(),
                             allowedSigners);
    }

    if ((mReviewRequest.ext.reviewDetails().tasksToAdd & CreateIssuanceRequestOpFrame::ISSUANCE_MANUAL_REVIEW_REQUIRED) != 0 ||
    (mReviewRequest.ext.reviewDetails().tasksToRemove & CreateIssuanceRequestOpFrame::ISSUANCE_MANUAL_REVIEW_REQUIRED) != 0)
    {
        return SourceDetails({AccountType::MASTER}, mSourceAccount->getHighThreshold(),
                static_cast<int32_t>(SignerType::SUPER_ISSUANCE_MANAGER));
    }

    return SourceDetails({AccountType::MASTER, AccountType::SYNDICATE}, mSourceAccount->getHighThreshold(),
                         allowedSigners);
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
			              CreateIssuanceRequestOpFrame::DEPOSIT_LIMIT_EXCEEDED;

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


bool ReviewIssuanceCreationRequestOpFrame::addStatistics(Database& db,
													   LedgerDelta& delta, LedgerManager& ledgerManager,
													   BalanceFrame::pointer balanceFrame, const uint64_t amountToAdd,
													   uint64_t& universalAmount)
{
	StatisticsV2Processor statisticsV2Processor(db, delta, ledgerManager);
	return tryAddStatsV2(statisticsV2Processor, balanceFrame, amountToAdd, universalAmount);
}
bool ReviewIssuanceCreationRequestOpFrame::tryAddStatsV2(StatisticsV2Processor& statisticsV2Processor,
                                                       const BalanceFrame::pointer balance, const uint64_t amountToAdd,
                                                       uint64_t& universalAmount)
{
	const auto result = statisticsV2Processor.addStatsV2(StatisticsV2Processor::SpendType::DEPOSIT, amountToAdd,
														 universalAmount, mSourceAccount, balance, nullptr);
	switch (result)
	{
		case StatisticsV2Processor::SUCCESS:
			return true;
		case StatisticsV2Processor::STATS_V2_OVERFLOW:
			return false;
		case StatisticsV2Processor::LIMITS_V2_EXCEEDED:
			return false;
		default:
			CLOG(ERROR, Logging::OPERATION_LOGGER)
					<< "Unexpeced result from statisticsV2Processor when updating statsV2:" << result;
			throw std::runtime_error("Unexpected state from statisticsV2Processor when updating statsV2");
	}

}

uint32_t ReviewIssuanceCreationRequestOpFrame::getSystemTasksToAdd( Application &app, Database& db,
		LedgerDelta &delta,
		LedgerManager &ledgerManager,
		ReviewableRequestFrame::pointer request)
	{
        // shield outer scope of any side effects by using
		// a sql transaction for ledger state and LedgerDelta
		soci::transaction localTx(db.getSession());
		LedgerDeltaImpl localDeltaImpl(delta);
		LedgerDelta& localDelta = localDeltaImpl;

		request->checkRequestType(ReviewableRequestType::ISSUANCE_CREATE);
		auto& requestEntry = request->getRequestEntry();
        auto& issuanceRequest = request->getRequestEntry().body.issuanceRequest();
		auto asset = AssetHelper::Instance()->mustLoadAsset(issuanceRequest.asset, db, &localDelta);

		uint32_t allTasks = 0;

        uint64_t universalAmount = 0;
        auto balanceFrame = AccountManager::loadOrCreateBalanceFrameForAsset(requestEntry.requestor, issuanceRequest.asset, db, localDelta);

		if (!asset->isAvailableForIssuanceAmountSufficient(issuanceRequest.amount))
		{
			allTasks |= CreateIssuanceRequestOpFrame::INSUFFICIENT_AVAILABLE_FOR_ISSUANCE_AMOUNT;
		}
		else
		{
			requestEntry.ext.tasksExt().pendingTasks &= ~CreateIssuanceRequestOpFrame::INSUFFICIENT_AVAILABLE_FOR_ISSUANCE_AMOUNT;
		}

		if (!addStatistics(db, localDelta, ledgerManager,
							   balanceFrame, issuanceRequest.amount,
							   universalAmount))
		{
			allTasks |= CreateIssuanceRequestOpFrame::DEPOSIT_LIMIT_EXCEEDED;
		}
		else
		{
			requestEntry.ext.tasksExt().pendingTasks &= ~CreateIssuanceRequestOpFrame::DEPOSIT_LIMIT_EXCEEDED;
		}

		if (allTasks == 0)
        {
		    localTx.commit();
		    localDelta.commit();
            return allTasks;
        }
        return allTasks;
	}

}
