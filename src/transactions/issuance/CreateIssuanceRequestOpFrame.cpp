// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include <transactions/review_request/ReviewRequestHelper.h>
#include <ledger/FeeHelper.h>
#include <transactions/ManageKeyValueOpFrame.h>
#include <ledger/KeyValueHelperLegacy.h>
#include "util/asio.h"
#include "CreateIssuanceRequestOpFrame.h"
#include "ledger/AccountHelper.h"
#include "ledger/AssetHelper.h"
#include "ledger/BalanceHelper.h"
#include "ledger/ReviewableRequestFrame.h"
#include "ledger/ReviewableRequestHelper.h"
#include "ledger/ReferenceFrame.h"
#include "util/Logging.h"
#include "util/types.h"
#include "database/Database.h"
#include "ledger/LedgerDelta.h"
#include "main/Application.h"
#include "crypto/SHA.h"
#include "xdrpp/marshal.h"
#include "xdrpp/printer.h"

namespace stellar
{

using namespace std;
using xdr::operator==;

const uint32_t CreateIssuanceRequestOpFrame::INSUFFICIENT_AVAILABLE_FOR_ISSUANCE_AMOUNT;
const uint32_t CreateIssuanceRequestOpFrame::ISSUANCE_MANUAL_REVIEW_REQUIRED;
const uint32_t CreateIssuanceRequestOpFrame::DEPOSIT_LIMIT_EXCEEDED;

CreateIssuanceRequestOpFrame::CreateIssuanceRequestOpFrame(Operation const& op,
                                       OperationResult& res,
                                       TransactionFrame& parentTx)
    : OperationFrame(op, res, parentTx)
    , mCreateIssuanceRequest(mOperation.body.createIssuanceRequestOp())
{
    mIsFeeRequired = true;
}

bool CreateIssuanceRequestOpFrame::doApplyV1(Application& app, LedgerDelta& delta, LedgerManager& ledgerManager)
{
	auto request = tryCreateIssuanceRequest(app, delta, ledgerManager);
	if (!request) {
		return false;
	}

    auto& database = app.getDatabase();
    const auto assetFrame = AssetHelper::Instance()->loadAsset(mCreateIssuanceRequest.request.asset, database);
    if (!assetFrame)
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Failed to load asset for issuance request. Asset Code: "
                                               << mCreateIssuanceRequest.request.asset;
        throw runtime_error("Failed to load asset for issuance request");
    }

    auto reviewResultCode = ReviewRequestResultCode::INSUFFICIENT_AVAILABLE_FOR_ISSUANCE_AMOUNT;
    if (!assetFrame->isPolicySet(AssetPolicy::ISSUANCE_MANUAL_REVIEW_REQUIRED))
    {
        reviewResultCode = ReviewRequestHelper::tryApproveRequest(mParentTx, app, ledgerManager, delta, request);
    }

	bool isFulfilled;
	switch (reviewResultCode) {
	case ReviewRequestResultCode::SUCCESS:
	{
		isFulfilled = true;
		break;
	}
	case ReviewRequestResultCode::INSUFFICIENT_AVAILABLE_FOR_ISSUANCE_AMOUNT:
	{
		isFulfilled = false;
		break;
	}
	case ReviewRequestResultCode::FULL_LINE:
	{
		innerResult().code(CreateIssuanceRequestResultCode::RECEIVER_FULL_LINE);
		return false;
	}
	default: {
		CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected result received on review of just created issuance request: " 
			<< xdr::xdr_traits<ReviewRequestResultCode>::enum_name(reviewResultCode);
		throw std::runtime_error("Unexpected result received on review of just created issuance request");
	}
	}

	innerResult().code(CreateIssuanceRequestResultCode::SUCCESS);
	innerResult().success().requestID = request->getRequestID();
	innerResult().success().fulfilled = isFulfilled;
    innerResult().success().fee = request->getRequestEntry().body.issuanceRequest().fee;
	auto& db = app.getDatabase();
	auto balanceHelper = BalanceHelper::Instance();
	auto receiver = balanceHelper->mustLoadBalance(mCreateIssuanceRequest.request.receiver, db);
	innerResult().success().receiver = receiver->getAccountID();
	return true;
}

bool CreateIssuanceRequestOpFrame::doApplyV2(Application& app, LedgerDelta& delta, LedgerManager& ledgerManager)
{
    auto requestFrame = tryCreateIssuanceRequest(app, delta, ledgerManager);
    if (!requestFrame)
    {
        return false;
    }

	auto& db = app.getDatabase();

    uint32_t allTasks = 0;
    if (!loadIssuanceTasks(db, allTasks))
    {
        innerResult().code(CreateIssuanceRequestResultCode::ISSUANCE_TASKS_NOT_FOUND);
        return false;
    }

    requestFrame->setTasks(allTasks);
    EntryHelperProvider::storeChangeEntry(delta, db, requestFrame->mEntry);

    const auto assetFrame = AssetHelper::Instance()->loadAsset(mCreateIssuanceRequest.request.asset, db);
    if (!assetFrame)
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Failed to load asset for issuance request. Asset Code: "
                                               << mCreateIssuanceRequest.request.asset;
        throw runtime_error("Failed to load asset for issuance request");
    }

	if (assetFrame->isPolicySet(AssetPolicy::ISSUANCE_MANUAL_REVIEW_REQUIRED))
	{
		allTasks |= ISSUANCE_MANUAL_REVIEW_REQUIRED;
		requestFrame->setTasks(allTasks);
		EntryHelperProvider::storeChangeEntry(delta, db, requestFrame->mEntry);
	}

    bool isFulfilled = false;
	ReviewRequestResultCode reviewRequestResultCode = ReviewRequestResultCode::SUCCESS;

	if (allTasks == 0)
	{
		ReviewRequestResult reviewRequestResult = ReviewRequestHelper::tryApproveRequestWithResult(mParentTx, app,
																								   ledgerManager, delta,
																								   requestFrame);
		reviewRequestResultCode = reviewRequestResult.code();
		isFulfilled = reviewRequestResult.success().ext.extendedResult().fulfilled;
	}

	switch (reviewRequestResultCode) {
		case ReviewRequestResultCode::SUCCESS:
		{
			break;
		}
		case ReviewRequestResultCode::FULL_LINE:
		{
			innerResult().code(CreateIssuanceRequestResultCode::RECEIVER_FULL_LINE);
			return false;
		}
		default:
		{
			CLOG(ERROR, Logging::OPERATION_LOGGER)
					<< "Unexpected result received on review of just created issuance request: "
					<< xdr::xdr_traits<ReviewRequestResultCode>::enum_name(reviewRequestResultCode);
			throw std::runtime_error("Unexpected result received on review of just created issuance request");
		}
	}

	innerResult().code(CreateIssuanceRequestResultCode::SUCCESS);
	innerResult().success().requestID = requestFrame->getRequestID();
	innerResult().success().fulfilled = isFulfilled;
	innerResult().success().fee = requestFrame->getRequestEntry().body.issuanceRequest().fee;
	auto balanceHelper = BalanceHelper::Instance();
	auto receiver = balanceHelper->mustLoadBalance(mCreateIssuanceRequest.request.receiver, db);
	innerResult().success().receiver = receiver->getAccountID();
    return true;
}

bool
CreateIssuanceRequestOpFrame::doApply(Application& app, LedgerDelta& delta, LedgerManager& ledgerManager)
{
	if (ledgerManager.shouldUse(LedgerVersion::ADD_TASKS_TO_REVIEWABLE_REQUEST))
	{
		return doApplyV2(app, delta, ledgerManager);
	}

	return doApplyV1(app, delta, ledgerManager);
}

bool
CreateIssuanceRequestOpFrame::doCheckValid(Application& app)
{
    
    if (!AssetFrame::isAssetCodeValid(mCreateIssuanceRequest.request.asset)) {
        innerResult().code(CreateIssuanceRequestResultCode::ASSET_NOT_FOUND);
        return false;
    }

	if (mCreateIssuanceRequest.request.amount == 0) {
		innerResult().code(CreateIssuanceRequestResultCode::INVALID_AMOUNT);
		return false;
	}

	if (mCreateIssuanceRequest.reference.empty()) {
		innerResult().code(CreateIssuanceRequestResultCode::REFERENCE_DUPLICATION);
		return false;
	}

	if (mCreateIssuanceRequest.request.externalDetails.size() > app.getIssuanceDetailsMaxLength()
            || !isValidJson(mCreateIssuanceRequest.request.externalDetails))
	{
		innerResult().code(CreateIssuanceRequestResultCode::INVALID_EXTERNAL_DETAILS);
        return false;
	}

	if (mCreateIssuanceRequest.ext.v() != LedgerVersion::ADD_TASKS_TO_REVIEWABLE_REQUEST)
	{
		return true;
	}

	int32_t systemTasks = CreateIssuanceRequestOpFrame::INSUFFICIENT_AVAILABLE_FOR_ISSUANCE_AMOUNT |
						  CreateIssuanceRequestOpFrame::ISSUANCE_MANUAL_REVIEW_REQUIRED |
						  CreateIssuanceRequestOpFrame::DEPOSIT_LIMIT_EXCEEDED;

	if (mCreateIssuanceRequest.ext.allTasks() && (*mCreateIssuanceRequest.ext.allTasks() & systemTasks) != 0)
	{
		innerResult().code(CreateIssuanceRequestResultCode::SYSTEM_TASKS_NOT_ALLOWED);
		return false;
	}
	
    return true;
}

std::unordered_map<AccountID, CounterpartyDetails> CreateIssuanceRequestOpFrame::getCounterpartyDetails(Database & db, LedgerDelta * delta) const
{
	// no counterparties
	return{};
}

SourceDetails CreateIssuanceRequestOpFrame::getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
                                                                    int32_t ledgerVersion) const
{
	if (ledgerVersion < static_cast<int32_t>(LedgerVersion::ADD_TASKS_TO_REVIEWABLE_REQUEST))
	{
		return SourceDetails({AccountType::MASTER, AccountType::SYNDICATE}, mSourceAccount->getHighThreshold(),
							 static_cast<int32_t>(SignerType::ISSUANCE_MANAGER));
	}

	if (mCreateIssuanceRequest.ext.v() == LedgerVersion::ADD_TASKS_TO_REVIEWABLE_REQUEST && !mCreateIssuanceRequest.ext.allTasks())
	{
		return SourceDetails({AccountType::MASTER, AccountType::SYNDICATE}, mSourceAccount->getHighThreshold(),
							 static_cast<uint32_t>(SignerType::ISSUANCE_MANAGER) |
							 static_cast<uint32_t>(SignerType::SUPER_ISSUANCE_MANAGER));
	}

	return SourceDetails({AccountType::MASTER, AccountType::SYNDICATE}, mSourceAccount->getHighThreshold(),
						 static_cast<uint32_t>(SignerType::SUPER_ISSUANCE_MANAGER));
}

bool CreateIssuanceRequestOpFrame::isAuthorizedToRequestIssuance(AssetFrame::pointer assetFrame)
{
	return assetFrame->getOwner() == getSourceID();
}

ReviewableRequestFrame::pointer CreateIssuanceRequestOpFrame::tryCreateIssuanceRequest(Application & app, LedgerDelta & delta, LedgerManager & ledgerManager)
{
	Database& db = ledgerManager.getDatabase();

	auto reviewableRequestHelper = ReviewableRequestHelper::Instance();
	if (reviewableRequestHelper->isReferenceExist(db, getSourceID(), mCreateIssuanceRequest.reference)) {
		innerResult().code(CreateIssuanceRequestResultCode::REFERENCE_DUPLICATION);
		return nullptr;
	}

	auto assetHelper = AssetHelper::Instance();
	auto asset = assetHelper->loadAsset(mCreateIssuanceRequest.request.asset, db);
	if (!asset) {
		innerResult().code(CreateIssuanceRequestResultCode::ASSET_NOT_FOUND);
		return nullptr;
	}

	if (!isAuthorizedToRequestIssuance(asset)) {
		innerResult().code(CreateIssuanceRequestResultCode::NOT_AUTHORIZED);
		return nullptr;
	}

	if (asset->willExceedMaxIssuanceAmount(mCreateIssuanceRequest.request.amount)) {
		innerResult().code(CreateIssuanceRequestResultCode::EXCEEDS_MAX_ISSUANCE_AMOUNT);
		return nullptr;
	}

	auto balanceHelper = BalanceHelper::Instance();
    auto balance = balanceHelper->loadBalance(mCreateIssuanceRequest.request.receiver, db);
	if (!balance || balance->getAsset() != asset->getCode()) {
		innerResult().code(CreateIssuanceRequestResultCode::NO_COUNTERPARTY);
		return nullptr;
	}

    if (!isAllowedToReceive(balance->getBalanceID(), db)) {
        return nullptr;
    }

    Fee feeToPay;
    if (!calculateFee(balance->getAccountID(), db, feeToPay)) {
        innerResult().code(CreateIssuanceRequestResultCode::FEE_EXCEEDS_AMOUNT);
        return nullptr;
    }

	auto reference = xdr::pointer<stellar::string64>(new stellar::string64(mCreateIssuanceRequest.reference));
	ReviewableRequestEntry::_body_t body;
	body.type(ReviewableRequestType::ISSUANCE_CREATE);
	body.issuanceRequest() = mCreateIssuanceRequest.request;
    body.issuanceRequest().fee = feeToPay;
	auto request = ReviewableRequestFrame::createNewWithHash(delta, getSourceID(), asset->getOwner(), reference,
                                                             body, ledgerManager.getCloseTime());
	EntryHelperProvider::storeAddEntry(delta, db, request->mEntry);
	return request;
}

bool CreateIssuanceRequestOpFrame::calculateFee(AccountID receiver, Database &db, Fee &fee)
{
    // calculate fee which will be charged from receiver
    fee.percent = 0;
    fee.fixed = 0;

    if (!mIsFeeRequired)
        return true;

    auto receiverFrame = AccountHelper::Instance()->mustLoadAccount(receiver, db);
    if (isSystemAccountType(receiverFrame->getAccountType()))
        return true;

    auto feeFrame = FeeHelper::Instance()->loadForAccount(FeeType::ISSUANCE_FEE, mCreateIssuanceRequest.request.asset,
                                                          FeeFrame::SUBTYPE_ANY, receiverFrame,
                                                          mCreateIssuanceRequest.request.amount, db);
    if (feeFrame) {
        fee.fixed = feeFrame->getFee().fixedFee;
        feeFrame->calculatePercentFee(mCreateIssuanceRequest.request.amount, fee.percent, ROUND_UP);
    }

    uint64_t totalFee = 0;
    if (!safeSum(fee.fixed, fee.percent, totalFee))
        throw std::runtime_error("totalFee overflows uint64");

    return totalFee < mCreateIssuanceRequest.request.amount;
}

CreateIssuanceRequestOp CreateIssuanceRequestOpFrame::build(
    AssetCode const& asset, const uint64_t amount, BalanceID const& receiver,
    LedgerManager& lm, uint32_t allTasks)
{
    IssuanceRequest request;
    request.amount = amount;
    request.asset = asset;
    request.externalDetails = "{}";
    request.fee.percent = 0;
    request.fee.fixed = 0;
    request.receiver = receiver;
    CreateIssuanceRequestOp issuanceRequestOp;
    issuanceRequestOp.request = request;
    issuanceRequestOp.reference = binToHex(sha256(xdr_to_opaque(receiver, asset, amount, lm.getCloseTime())));

    if (lm.shouldUse(LedgerVersion::ADD_TASKS_TO_REVIEWABLE_REQUEST))
    {
        issuanceRequestOp.ext.v(LedgerVersion::ADD_TASKS_TO_REVIEWABLE_REQUEST);
        issuanceRequestOp.ext.allTasks().activate() = allTasks;
    }

    return issuanceRequestOp;
}

bool
CreateIssuanceRequestOpFrame::isAllowedToReceive(BalanceID receivingBalance, Database &db)
{
	const auto result = AccountManager::isAllowedToReceive(receivingBalance, db);
	switch (result){
		case AccountManager::SUCCESS:
			return true;
		case AccountManager::BALANCE_NOT_FOUND:
			innerResult().code(CreateIssuanceRequestResultCode::NO_COUNTERPARTY);
			return false;
		case AccountManager::REQUIRED_VERIFICATION:
			innerResult().code(CreateIssuanceRequestResultCode::REQUIRES_VERIFICATION);
			return false;
		case AccountManager::REQUIRED_KYC:
			innerResult().code(CreateIssuanceRequestResultCode::REQUIRES_KYC);
			return false;
		default:
			CLOG(ERROR, Logging::OPERATION_LOGGER)
					<< "Unexpected isAllowedToReceive method result from accountManager:" << result;
			throw std::runtime_error("Unexpected isAllowedToReceive method result from accountManager");
	}
}

bool CreateIssuanceRequestOpFrame::loadIssuanceTasks(Database &db, uint32_t &allTasks)
{
    if (mCreateIssuanceRequest.ext.v() == LedgerVersion::ADD_TASKS_TO_REVIEWABLE_REQUEST && mCreateIssuanceRequest.ext.allTasks())
    {
        allTasks = *mCreateIssuanceRequest.ext.allTasks().get();
        return true;
    }

    auto key = ManageKeyValueOpFrame::makeIssuanceTasksKey(mCreateIssuanceRequest.request.asset);
    auto keyValueFrame = KeyValueHelperLegacy::Instance()->loadKeyValue(key, db);
    if (!keyValueFrame)
    {
        return false;
    }

    if (keyValueFrame->getKeyValue().value.type() != KeyValueEntryType::UINT32)
    {
        throw std::runtime_error("Unexpected database state, expected issuance tasks to be UINT32");
    }

    allTasks = keyValueFrame->getKeyValue().value.ui32Value();
    return true;
}

}
