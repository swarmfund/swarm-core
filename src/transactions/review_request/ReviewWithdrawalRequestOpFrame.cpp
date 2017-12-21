// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "util/asio.h"
#include "ReviewWithdrawalRequestOpFrame.h"
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

namespace stellar
{
using namespace std;
using xdr::operator==;

bool ReviewWithdrawalRequestOpFrame::handleApprove(
    Application& app, LedgerDelta& delta, LedgerManager& ledgerManager,
    ReviewableRequestFrame::pointer request)
{
    if (request->getRequestType() != ReviewableRequestType::WITHDRAW)
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) <<
            "Unexpected request type. Expected WITHDRAW, but got " << xdr::
            xdr_traits<ReviewableRequestType>::
            enum_name(request->getRequestType());
        throw
            invalid_argument("Unexpected request type for review withdraw request");
    }

    auto& withdrawRequest = request->getRequestEntry().body.withdrawalRequest();

    Database& db = ledgerManager.getDatabase();
    EntryHelperProvider::storeDeleteEntry(delta, db, request->getKey());
 
    auto balance = BalanceHelper::Instance()->mustLoadBalance(withdrawRequest.balance, db, &delta);
    const auto totalAmountToCharge = getTotalAmountToCharge(request->getRequestID(), withdrawRequest);
    if (!balance->tryChargeFromLocked(totalAmountToCharge))
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected db state. Failed to charge from locked amount which must be locked. requestID: " << request->getRequestID();
        throw runtime_error("Unexected db state. Failed to charge from locked");
    }
    EntryHelperProvider::storeChangeEntry(delta, db, balance->mEntry);

    transferFee(app, db, delta, request, balance);

    auto assetFrame = AssetHelper::Instance()->loadAsset(balance->getAsset(), db, &delta);
    if (!assetFrame)
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Failed to load asset for withdrawal request" << request->getRequestID();
        throw runtime_error("Failed to load asset for withdrawal request");
    }

    if (!assetFrame->tryWithdraw(withdrawRequest.amount))
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected state: failed to withdraw from asset for request: " << request->getRequestID();
        throw runtime_error("Failed to withdraw from asset");
    }

    AssetHelper::Instance()->storeChange(delta, db, assetFrame->mEntry);

    innerResult().code(ReviewRequestResultCode::SUCCESS);
    return true;
}

bool ReviewWithdrawalRequestOpFrame::handleReject(
    Application& app, LedgerDelta& delta, LedgerManager& ledgerManager,
    ReviewableRequestFrame::pointer request)
{
    innerResult().code(ReviewRequestResultCode::REJECT_NOT_ALLOWED);
    return false;
}

SourceDetails ReviewWithdrawalRequestOpFrame::getSourceAccountDetails(
    std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails)
const
{
    return SourceDetails({AccountType::MASTER, AccountType::SYNDICATE},
                         mSourceAccount->getHighThreshold(),
                         static_cast<int32_t>(SignerType::ASSET_MANAGER));
}

uint64_t ReviewWithdrawalRequestOpFrame::getTotalFee(const uint64_t requestID, WithdrawalRequest& withdrawRequest)
{
    uint64_t totalFee;
    if (!safeSum(withdrawRequest.fee.percent, withdrawRequest.fee.fixed, totalFee))
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Failed to calculate total fee for withdrawal request: " << requestID;
        throw runtime_error("Failed to calculate total fee for withdrawal request");
    }

    return totalFee;
}

uint64_t ReviewWithdrawalRequestOpFrame::getTotalAmountToCharge(
    const uint64_t requestID, WithdrawalRequest& withdrawalRequest)
{
    const auto totalFee = getTotalFee(requestID, withdrawalRequest);
    uint64_t totalAmountToCharge;
    if (!safeSum(withdrawalRequest.amount, totalFee, totalAmountToCharge))
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Failed to calculate total amount ot be charged for withdrawal request: " << requestID;
        throw runtime_error("Failed to calculate total amount to be charged for withdrawal request");
    }

    return totalAmountToCharge;
}

void ReviewWithdrawalRequestOpFrame::transferFee(Application& app, Database& db, LedgerDelta& delta, ReviewableRequestFrame::pointer request,
    BalanceFrame::pointer balance)
{
    auto withdrawRequest = request->getRequestEntry().body.withdrawalRequest();
    const auto totalFee = getTotalFee(request->getRequestID(), withdrawRequest);
    if (totalFee == 0)
    {
	return;
    }
    
    auto commissionBalance = BalanceHelper::Instance()->loadBalance(app.getCommissionID(), balance->getAsset(), db, &delta);
    if (!commissionBalance)
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected state: reviewing withdrwal requet - and there is no commission balance for that asset" << request->getRequestID();
        throw runtime_error("Unexpected state: no commission balance");
    }

    if (!commissionBalance->tryFundAccount(totalFee))
    {
	CLOG(ERROR, Logging::OPERATION_LOGGER) << "Failed to fund commission balance with fee for withdrawal - overflow" << request->getRequestID();
	throw runtime_error("Failed to fund commission balance with fee");
    }

    EntryHelperProvider::storeChangeEntry(delta, db, commissionBalance->mEntry);
}

ReviewWithdrawalRequestOpFrame::ReviewWithdrawalRequestOpFrame(
    Operation const& op, OperationResult& res, TransactionFrame& parentTx) :
                                                                           ReviewRequestOpFrame(op,
                                                                                                res,
                                                                                                parentTx)
{
}

bool ReviewWithdrawalRequestOpFrame::handlePermanentReject(Application& app,
    LedgerDelta& delta, LedgerManager& ledgerManager,
    ReviewableRequestFrame::pointer request)
{
    if (request->getRequestType() != ReviewableRequestType::WITHDRAW)
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) <<
            "Unexpected request type. Expected WITHDRAW, but got " << xdr::
            xdr_traits<ReviewableRequestType>::
            enum_name(request->getRequestType());
        throw
            invalid_argument("Unexpected request type for review withdraw request");
    }

    auto& withdrawRequest = request->getRequestEntry().body.withdrawalRequest();
    Database& db = app.getDatabase();
    auto balance = BalanceHelper::Instance()->mustLoadBalance(withdrawRequest.balance, db, &delta);
    const auto totalAmountToCharge = getTotalAmountToCharge(request->getRequestID(), withdrawRequest);
    if (!balance->unlock(totalAmountToCharge))
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected db state. Failed to unlock locked amount. requestID: " << request->getRequestID();
        throw runtime_error("Unexected db state. Failed to unlock locked amount");
    }

    uint64_t universalAmount = request->getRequestEntry().body.withdrawalRequest().universalAmount;
    if (universalAmount > 0)
    {
        AccountManager accountManager(app, ledgerManager.getDatabase(), delta, ledgerManager);
        AccountID requestor = request->getRequestor();
        time_t timePerformed = request->getCreatedAt();
        accountManager.revertStats(requestor, universalAmount, timePerformed);
    }

    EntryHelperProvider::storeChangeEntry(delta, db, balance->mEntry);
    return ReviewRequestOpFrame::handlePermanentReject(app, delta, ledgerManager, request);
}
}
