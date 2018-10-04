// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "util/asio.h"
#include "ReviewTwoStepWithdrawalRequestOpFrame.h"
#include "util/Logging.h"
#include "util/types.h"
#include "database/Database.h"
#include "ledger/LedgerDelta.h"
#include "ledger/ReviewableRequestFrame.h"
#include "ledger/ReferenceFrame.h"
#include "ledger/AssetHelperLegacy.h"
#include "ledger/BalanceHelperLegacy.h"
#include "main/Application.h"
#include "xdrpp/printer.h"
#include "ledger/ReviewableRequestHelper.h"

namespace stellar
{
using namespace std;
using xdr::operator==;

bool ReviewTwoStepWithdrawalRequestOpFrame::handleApprove(
    Application& app, LedgerDelta& delta, LedgerManager& ledgerManager,
    ReviewableRequestFrame::pointer request)
{
    
    if (request->getRequestType() != ReviewableRequestType::TWO_STEP_WITHDRAWAL)
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) <<
            "Unexpected request type. Expected TWO_STEP_WITHDRAWAL, but got " << xdr::
            xdr_traits<ReviewableRequestType>::
            enum_name(request->getRequestType());
        throw
            invalid_argument("Unexpected request type for review two step withdraw request");
    }

    auto withdrawRequest = request->getRequestEntry().body.twoStepWithdrawalRequest();
    withdrawRequest.preConfirmationDetails = mReviewRequest.requestDetails.twoStepWithdrawal().externalDetails;
    request->getRequestEntry().body.type(ReviewableRequestType::WITHDRAW);
    request->getRequestEntry().body.withdrawalRequest() = withdrawRequest;
    request->recalculateHashRejectReason();
    auto& db = app.getDatabase();
    ReviewableRequestHelper::Instance()->storeChange(delta, db, request->mEntry);

    innerResult().code(ReviewRequestResultCode::SUCCESS);
    return true;
}

bool ReviewTwoStepWithdrawalRequestOpFrame::handleReject(
    Application& app, LedgerDelta& delta, LedgerManager& ledgerManager,
    ReviewableRequestFrame::pointer request)
{
    innerResult().code(ReviewRequestResultCode::REJECT_NOT_ALLOWED);
    return false;
}

SourceDetails ReviewTwoStepWithdrawalRequestOpFrame::getSourceAccountDetails(
    std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails, int32_t ledgerVersion)
const
{
    auto allowedSigners = static_cast<int32_t>(SignerType::ASSET_MANAGER);

    auto newSingersVersion = static_cast<int32_t>(LedgerVersion::NEW_SIGNER_TYPES);
    if (ledgerVersion >= newSingersVersion)
    {
        allowedSigners = static_cast<int32_t>(SignerType::WITHDRAW_MANAGER);
    }

    return SourceDetails({AccountType::MASTER, AccountType::SYNDICATE},
                         mSourceAccount->getHighThreshold(), allowedSigners);
}

uint64_t ReviewTwoStepWithdrawalRequestOpFrame::getTotalFee(const uint64_t requestID, WithdrawalRequest& withdrawRequest)
{
    uint64_t totalFee;
    if (!safeSum(withdrawRequest.fee.percent, withdrawRequest.fee.fixed, totalFee))
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Failed to calculate total fee for withdrawal request: " << requestID;
        throw runtime_error("Failed to calculate total fee for withdrawal request");
    }

    return totalFee;
}

uint64_t ReviewTwoStepWithdrawalRequestOpFrame::getTotalAmountToCharge(
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

    ReviewTwoStepWithdrawalRequestOpFrame::ReviewTwoStepWithdrawalRequestOpFrame(
    Operation const& op, OperationResult& res, TransactionFrame& parentTx) :
                                                                           ReviewRequestOpFrame(op,
                                                                                                res,
                                                                                                parentTx)
{
}

bool ReviewTwoStepWithdrawalRequestOpFrame::rejectWithdrawalRequest(Application& app, LedgerDelta& delta, LedgerManager& ledgerManager, ReviewableRequestFrame::pointer request, WithdrawalRequest& withdrawRequest)
{
    Database& db = app.getDatabase();
    auto balance = BalanceHelperLegacy::Instance()->mustLoadBalance(withdrawRequest.balance, db, &delta);
    const auto totalAmountToCharge = getTotalAmountToCharge(request->getRequestID(), withdrawRequest);
    if (!balance->unlock(totalAmountToCharge))
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected db state. Failed to unlock locked amount. requestID: " << request->getRequestID();
        throw runtime_error("Unexpected db state. Failed to unlock locked amount");
    }

    const uint64_t universalAmount = withdrawRequest.universalAmount;
    if (universalAmount > 0)
    {
        if (!ledgerManager.shouldUse(LedgerVersion::CREATE_ONLY_STATISTICS_V2))
        {
            AccountManager accountManager(app, ledgerManager.getDatabase(), delta, ledgerManager);
            const AccountID requestor = request->getRequestor();
            const time_t timePerformed = request->getCreatedAt();
            accountManager.revertStats(requestor, universalAmount, timePerformed);
        }
        else
        {
            StatisticsV2Processor statisticsV2Processor(ledgerManager.getDatabase(), delta, ledgerManager);
            statisticsV2Processor.revertStatsV2(request->getRequestID());
        }
    }

    EntryHelperProvider::storeChangeEntry(delta, db, balance->mEntry);
    return ReviewRequestOpFrame::handlePermanentReject(app, delta, ledgerManager, request);
}

bool ReviewTwoStepWithdrawalRequestOpFrame::handlePermanentReject(Application& app,
    LedgerDelta& delta, LedgerManager& ledgerManager,
    ReviewableRequestFrame::pointer request)
{
    if (request->getRequestType() != ReviewableRequestType::TWO_STEP_WITHDRAWAL)
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) <<
            "Unexpected request type. Expected WITHDRAW, but got " << xdr::
            xdr_traits<ReviewableRequestType>::
            enum_name(request->getRequestType());
        throw
            invalid_argument("Unexpected request type for review withdraw request");
    }

    auto& withdrawRequest = request->getRequestEntry().body.twoStepWithdrawalRequest();
    return rejectWithdrawalRequest(app, delta, ledgerManager, request, withdrawRequest);
}
}
