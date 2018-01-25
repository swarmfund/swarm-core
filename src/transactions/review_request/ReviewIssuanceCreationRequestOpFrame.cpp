// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include <ledger/AccountHelper.h>
#include "util/asio.h"
#include "ReviewIssuanceCreationRequestOpFrame.h"
#include "util/Logging.h"
#include "util/types.h"
#include "database/Database.h"
#include "ledger/LedgerDelta.h"
#include "ledger/ReviewableRequestFrame.h"
#include "ledger/ReferenceFrame.h"
#include "ledger/AssetHelper.h"
#include "ledger/BalanceHelper.h"
#include "main/Application.h"
#include "xdrpp/printer.h"
#include "ReviewRequestHelper.h"

namespace stellar
{

using namespace std;
using xdr::operator==;

bool ReviewIssuanceCreationRequestOpFrame::handleApprove(Application & app, LedgerDelta & delta, LedgerManager & ledgerManager, ReviewableRequestFrame::pointer request)
{
	if (request->getRequestType() != ReviewableRequestType::ISSUANCE_CREATE) {
		CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected request type. Expected ISSUANCE_CREATE, but got " << xdr::xdr_traits<ReviewableRequestType>::enum_name(request->getRequestType());
		throw std::invalid_argument("Unexpected request type for review issuance creation request");
	}

	auto issuanceCreationRequest = request->getRequestEntry().body.issuanceRequest();
	Database& db = ledgerManager.getDatabase();
	createReference(delta, db, request->getRequestor(), request->getReference());	

	auto assetHelper = AssetHelper::Instance();
	auto asset = assetHelper->loadAsset(issuanceCreationRequest.asset, db, &delta);
	if (!asset) {
		CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected state. Expected asset to exist for issuance request. Request: " << xdr::xdr_to_string(request->getRequestEntry());
		throw std::runtime_error("Expected asset for  issuance request to exist");
	}

	if (asset->willExceedMaxIssuanceAmount(issuanceCreationRequest.amount)) {
		innerResult().code(ReviewRequestResultCode::MAX_ISSUANCE_AMOUNT_EXCEEDED);
		return false;
	}

	if (!asset->isAvailableForIssuanceAmountSufficient(issuanceCreationRequest.amount)) {
		innerResult().code(ReviewRequestResultCode::INSUFFICIENT_AVAILABLE_FOR_ISSUANCE_AMOUNT);
		return false;
	}

	if (!asset->tryIssue(issuanceCreationRequest.amount)) {
		CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected state. Failed to fulfill request: " << xdr::xdr_to_string(request->getRequestEntry());
		throw std::runtime_error("Unexcpted issuance result. Expected to be able to issue");
	}

	EntryHelperProvider::storeChangeEntry(delta, db, asset->mEntry);

	auto balanceHelper = BalanceHelper::Instance();
	auto receiver = balanceHelper->loadBalance(issuanceCreationRequest.receiver, db, &delta);
	if (!receiver) {
		CLOG(ERROR, Logging::OPERATION_LOGGER) << "Excpected receiver to exist: " << xdr::xdr_to_string(request->getRequestEntry());
		throw std::runtime_error("Expected receiver to exist");
	}

    uint64_t totalFee = 0;
    if (!safeSum(issuanceCreationRequest.fee.fixed, issuanceCreationRequest.fee.percent, totalFee)) {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "totalFee overflows uint64 for request: " << request->getRequestID();
        throw std::runtime_error("totalFee overflows uint64");
    }

    if (totalFee >= issuanceCreationRequest.amount) {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected state. totalFee exceeds amount for request: "
                                               << request->getRequestID();
        throw std::runtime_error("Unexpected state. totalFee exceeds amount");
    }

    auto receiverAccount = AccountHelper::Instance()->mustLoadAccount(receiver->getAccountID(), db);
    if (!AccountManager::isAllowedToReceive(receiver->getBalanceID(), db)) {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Asset requires receiver account to have KYC" << request->getRequestID();
        throw std::runtime_error("Unexpeced state. Asset requires KYC but account is NOT_VERIFIED");
    }

    //transfer fee
    AccountManager accountManager(app, db, delta, ledgerManager);
    accountManager.transferFee(issuanceCreationRequest.asset, totalFee);

    uint64_t destinationReceive = issuanceCreationRequest.amount - totalFee;
    if (!receiver->tryFundAccount(destinationReceive)) {
        innerResult().code(ReviewRequestResultCode::FULL_LINE);
        return false;
    }

	EntryHelperProvider::storeChangeEntry(delta, db, receiver->mEntry);

	EntryHelperProvider::storeDeleteEntry(delta, db, request->getKey());
	innerResult().code(ReviewRequestResultCode::SUCCESS);
	return true;
}

bool ReviewIssuanceCreationRequestOpFrame::handleReject(Application & app, LedgerDelta & delta, LedgerManager & ledgerManager, ReviewableRequestFrame::pointer request)
{
	innerResult().code(ReviewRequestResultCode::REJECT_NOT_ALLOWED);
	return false;
}

SourceDetails ReviewIssuanceCreationRequestOpFrame::getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails) const
{
	return SourceDetails({AccountType::MASTER}, mSourceAccount->getHighThreshold(),
						 static_cast<int32_t>(SignerType::ASSET_MANAGER));
}

ReviewIssuanceCreationRequestOpFrame::ReviewIssuanceCreationRequestOpFrame(Operation const & op, OperationResult & res, TransactionFrame & parentTx) :
	ReviewRequestOpFrame(op, res, parentTx)
{
}

}
