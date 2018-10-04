// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ledger/StatisticsHelper.h"
#include "ledger/LedgerDelta.h"
#include "ledger/AccountHelper.h"
#include "ledger/BalanceHelperLegacy.h"
#include "ledger/AssetHelperLegacy.h"
#include "ledger/AssetPairHelper.h"
#include "ledger/ReviewableRequestFrame.h"
#include "ledger/KeyValueHelperLegacy.h"
#include "ledger/ReviewableRequestHelper.h"
#include "transactions/CreateWithdrawalRequestOpFrame.h"
#include "database/Database.h"
#include "main/Application.h"
#include "medida/metrics_registry.h"
#include "xdrpp/printer.h"
#include "StatisticsV2Processor.h"

namespace stellar
{
using xdr::operator==;


std::unordered_map<AccountID, CounterpartyDetails>
CreateWithdrawalRequestOpFrame::getCounterpartyDetails(
    Database& db, LedgerDelta* delta) const
{
    // source account is only counterparty
    return {};
}

SourceDetails CreateWithdrawalRequestOpFrame::getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
                                                                      int32_t ledgerVersion)
const
{
    return SourceDetails({
                             AccountType::GENERAL, AccountType::SYNDICATE,
                             AccountType::OPERATIONAL, AccountType::EXCHANGE, AccountType::NOT_VERIFIED,
                             AccountType::ACCREDITED_INVESTOR, AccountType::INSTITUTIONAL_INVESTOR,
                             AccountType::VERIFIED
                         }, mSourceAccount->getMediumThreshold(),
                         static_cast<int32_t>(SignerType::BALANCE_MANAGER),
                         static_cast<int32_t>(BlockReasons::TOO_MANY_KYC_UPDATE_REQUESTS));
}

BalanceFrame::pointer CreateWithdrawalRequestOpFrame::tryLoadBalance(
    Database& db, LedgerDelta& delta) const
{
    auto balanceFrame = BalanceHelperLegacy::Instance()->loadBalance(mCreateWithdrawalRequest.request.balance, db, &delta);
    if (!balanceFrame || !(balanceFrame->getAccountID() == getSourceID()))
    {
        return nullptr;
    }

    return balanceFrame;
}

bool CreateWithdrawalRequestOpFrame::isFeeMatches(
    AccountManager& accountManager, BalanceFrame::pointer balance) const
{
    return accountManager.isFeeMatches(mSourceAccount, mCreateWithdrawalRequest.request.fee, FeeType::WITHDRAWAL_FEE,
        FeeFrame::SUBTYPE_ANY, balance->getAsset(), mCreateWithdrawalRequest.request.amount);
}

bool CreateWithdrawalRequestOpFrame::isConvertedAmountMatches(
    BalanceFrame::pointer balance, Database& db)
{
    const auto assetToConvertAmountInto = mCreateWithdrawalRequest.request.details.autoConversion().destAsset;

    if(balance->getAsset() == assetToConvertAmountInto) {
        if (mCreateWithdrawalRequest.request.amount !=
                mCreateWithdrawalRequest.request.details.autoConversion().expectedAmount) {
            innerResult().code(CreateWithdrawalRequestResultCode::CONVERTED_AMOUNT_MISMATCHED);
            return false;
        }
        return true;
    }

    const auto assetPair = AssetPairHelper::Instance()->tryLoadAssetPairForAssets(balance->getAsset(), assetToConvertAmountInto, db);
    if (!assetPair)
    {
        innerResult().code(CreateWithdrawalRequestResultCode::CONVERSION_PRICE_IS_NOT_AVAILABLE);
        return false;
    }

    uint64_t expectedConvetedAmount = 0;
    if (!assetPair->convertAmount(assetToConvertAmountInto, mCreateWithdrawalRequest.request.amount, Rounding::ROUND_DOWN, expectedConvetedAmount))
    {
        innerResult().code(CreateWithdrawalRequestResultCode::CONVERSION_OVERFLOW);
        return false;
    }

    if (expectedConvetedAmount != mCreateWithdrawalRequest.request.details.autoConversion().expectedAmount)
    {
        innerResult().code(CreateWithdrawalRequestResultCode::CONVERTED_AMOUNT_MISMATCHED);
        return false;
    }

    return true;
}

bool CreateWithdrawalRequestOpFrame::tryLockBalance(
    BalanceFrame::pointer balance)
{
    uint64_t totalAmountToBeLocked = 0;
    auto& request = mCreateWithdrawalRequest.request;
    if (!safeSum(totalAmountToBeLocked, { request.amount, request.fee.fixed, request.fee.percent }))
    {
        innerResult().code(CreateWithdrawalRequestResultCode::BALANCE_LOCK_OVERFLOW);
        return false;
    }

    const auto balanceLockResult = balance->tryLock(totalAmountToBeLocked);
    switch (balanceLockResult)
    {
    case BalanceFrame::Result::SUCCESS:
        return true;
    case BalanceFrame::Result::LINE_FULL:
        {
        innerResult().code(CreateWithdrawalRequestResultCode::BALANCE_LOCK_OVERFLOW);
        return false;
        }
    case BalanceFrame::Result::UNDERFUNDED:
        {
        innerResult().code(CreateWithdrawalRequestResultCode::UNDERFUNDED);
        return false;
        }
    default:
        {
            CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected balance lock result for create withdrawal request: " << balanceLockResult;
            throw std::runtime_error("Unexpected result on balance lock");
        }
    }
}

CreateWithdrawalRequestOpFrame::CreateWithdrawalRequestOpFrame(
    Operation const& op, OperationResult& res,
    TransactionFrame& parentTx)
    : OperationFrame(op, res, parentTx)
    , mCreateWithdrawalRequest(mOperation.body.createWithdrawalRequestOp())
{
}


ReviewableRequestFrame::pointer
CreateWithdrawalRequestOpFrame::createRequest(LedgerDelta& delta, LedgerManager& ledgerManager,
                                              Database& db, const AssetFrame::pointer assetFrame,
                                              const uint64_t universalAmount)
{
    auto request = ReviewableRequestFrame::createNew(delta, getSourceID(), assetFrame->getOwner(), nullptr,
                                                     ledgerManager.getCloseTime());
    ReviewableRequestEntry &requestEntry = request->getRequestEntry();
    if (assetFrame->isPolicySet(AssetPolicy::TWO_STEP_WITHDRAWAL)) {
        requestEntry.body.type(ReviewableRequestType::TWO_STEP_WITHDRAWAL);
        requestEntry.body.twoStepWithdrawalRequest() = mCreateWithdrawalRequest.request;
        requestEntry.body.twoStepWithdrawalRequest().universalAmount = universalAmount;
    } else {
        requestEntry.body.type(ReviewableRequestType::WITHDRAW);
        requestEntry.body.withdrawalRequest() = mCreateWithdrawalRequest.request;
        requestEntry.body.withdrawalRequest().universalAmount = universalAmount;
    }

    request->recalculateHashRejectReason();
    ReviewableRequestHelper::Instance()->storeAdd(delta, db, request->mEntry);
    return request;
}

void
CreateWithdrawalRequestOpFrame::storeChangeRequest(LedgerDelta& delta, ReviewableRequestFrame::pointer request,
                                                   Database& db, const uint64_t universalAmount)
{
    switch (request->getRequestEntry().body.type())
    {
        case ReviewableRequestType::TWO_STEP_WITHDRAWAL:
        {
            request->getRequestEntry().body.twoStepWithdrawalRequest().universalAmount = universalAmount;
            break;
        };
        case ReviewableRequestType::WITHDRAW:
        {
            request->getRequestEntry().body.withdrawalRequest().universalAmount = universalAmount;
            break;
        };
        default:
            throw std::runtime_error("Unexpected reviewable request type");
    }

    request->recalculateHashRejectReason();
    ReviewableRequestHelper::Instance()->storeChange(delta, db, request->mEntry);
}

ReviewableRequestFrame::pointer
CreateWithdrawalRequestOpFrame::approveRequest(AccountManager& accountManager, LedgerDelta& delta,
                                               LedgerManager& ledgerManager, Database& db,
                                               const AssetFrame::pointer assetFrame,
                                               const BalanceFrame::pointer balanceFrame)
{
    uint64_t universalAmount = 0;
    auto request = createRequest(delta, ledgerManager, db, assetFrame, universalAmount);

    if (!processStatistics(accountManager, db, delta, ledgerManager, balanceFrame,
                           mCreateWithdrawalRequest.request.amount, universalAmount, request->getRequestID()))
    {
        return nullptr;
    }
    storeChangeRequest(delta, request, db, universalAmount);

    return request;
}


bool
CreateWithdrawalRequestOpFrame::doApply(Application& app, LedgerDelta& delta,
                                        LedgerManager& ledgerManager)
{
    auto& db = ledgerManager.getDatabase();
    auto balanceFrame = tryLoadBalance(db, delta);
    if (!balanceFrame)
    {
        innerResult().code(CreateWithdrawalRequestResultCode::BALANCE_NOT_FOUND);
        return false;
    }

    const auto assetFrame = AssetHelperLegacy::Instance()->mustLoadAsset(balanceFrame->getAsset(), db);
    if (!assetFrame->isPolicySet(AssetPolicy::WITHDRAWABLE))
    {
        innerResult().code(CreateWithdrawalRequestResultCode::ASSET_IS_NOT_WITHDRAWABLE);
        return false;
    }

    auto code = assetFrame->getAsset().code;
    if (!exceedsLowerBound(db, code))
    {
        innerResult().code(CreateWithdrawalRequestResultCode::LOWER_BOUND_NOT_EXCEEDED);
        return false;
    }

    AccountManager accountManager(app, db, delta, ledgerManager);
    if (!isFeeMatches(accountManager, balanceFrame))
    {
        innerResult().code(CreateWithdrawalRequestResultCode::FEE_MISMATCHED);
        return false;
    }

    if (!isConvertedAmountMatches(balanceFrame, db))
    {
        return false;
    }


    if (!tryLockBalance(balanceFrame))
    {
        return false;
    }

    auto request = approveRequest(accountManager, delta, ledgerManager, db, assetFrame, balanceFrame);
    if (!request)
        return false;

    BalanceHelperLegacy::Instance()->storeChange(delta, db, balanceFrame->mEntry);

    innerResult().code(CreateWithdrawalRequestResultCode::SUCCESS);
    innerResult().success().requestID = request->getRequestID();
    return true;
}

bool CreateWithdrawalRequestOpFrame::doCheckValid(Application& app)
{
    if (mCreateWithdrawalRequest.request.amount == 0)
    {
        innerResult().code(CreateWithdrawalRequestResultCode::INVALID_AMOUNT);
        return false;
    }

    if (mCreateWithdrawalRequest.request.universalAmount != 0)
    {
        innerResult().code(CreateWithdrawalRequestResultCode::INVALID_UNIVERSAL_AMOUNT);
        return false;
    }

    if (!mCreateWithdrawalRequest.request.preConfirmationDetails.empty())
    {
        innerResult().code(CreateWithdrawalRequestResultCode::INVALID_PRE_CONFIRMATION_DETAILS);
        return false;
    }

    if (!isExternalDetailsValid(app, mCreateWithdrawalRequest.request.externalDetails)) {
        innerResult().code(CreateWithdrawalRequestResultCode::INVALID_EXTERNAL_DETAILS);
        return false;
    }

    return true;
}

bool CreateWithdrawalRequestOpFrame::isExternalDetailsValid(Application &app, const std::string &externalDetails) {
    if (!isValidJson(externalDetails))
        return false;

    return externalDetails.size() <= app.getWithdrawalDetailsMaxLength();
}

bool CreateWithdrawalRequestOpFrame::processStatistics(AccountManager& accountManager, Database& db,
                                                       LedgerDelta& delta, LedgerManager& ledgerManager,
                                                       BalanceFrame::pointer balanceFrame, const uint64_t amountToAdd,
                                                       uint64_t& universalAmount, const uint64_t requestID)
{
    if (!ledgerManager.shouldUse(LedgerVersion::CREATE_ONLY_STATISTICS_V2))
    {
        if (!tryAddStats(accountManager, balanceFrame, amountToAdd, universalAmount))
            return false;
    }

    StatisticsV2Processor statisticsV2Processor(db, delta, ledgerManager);
    return tryAddStatsV2(statisticsV2Processor, balanceFrame, amountToAdd, universalAmount, requestID);
}

bool CreateWithdrawalRequestOpFrame::tryAddStats(AccountManager& accountManager, const BalanceFrame::pointer balance,
                                                 const uint64_t amountToAdd, uint64_t& universalAmount)
{
    const auto result = accountManager.addStats(mSourceAccount, balance, amountToAdd, universalAmount);
    switch (result) {
        case AccountManager::SUCCESS:
            return true;
        case AccountManager::STATS_OVERFLOW:
            innerResult().code(CreateWithdrawalRequestResultCode::STATS_OVERFLOW);
            return false;
        case AccountManager::LIMITS_EXCEEDED:
            innerResult().code(CreateWithdrawalRequestResultCode::LIMITS_EXCEEDED);
            return false;
        default:
            CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpeced result from accountManager when updating stats:" << result;
            throw std::runtime_error("Unexpected state from accountManager when updating stats");
    }
}

bool CreateWithdrawalRequestOpFrame::tryAddStatsV2(StatisticsV2Processor& statisticsV2Processor,
                                                   const BalanceFrame::pointer balance, const uint64_t amountToAdd,
                                                   uint64_t& universalAmount, uint64_t requestID)
{
    const auto result = statisticsV2Processor.addStatsV2(StatisticsV2Processor::SpendType::WITHDRAW, amountToAdd,
                                                         universalAmount, mSourceAccount, balance, &requestID);
    switch (result)
    {
        case StatisticsV2Processor::SUCCESS:
            return true;
        case StatisticsV2Processor::STATS_V2_OVERFLOW:
            innerResult().code(CreateWithdrawalRequestResultCode::STATS_OVERFLOW);
            return false;
        case StatisticsV2Processor::LIMITS_V2_EXCEEDED:
            innerResult().code(CreateWithdrawalRequestResultCode::LIMITS_EXCEEDED);
            return false;
        default:
            CLOG(ERROR, Logging::OPERATION_LOGGER)
                    << "Unexpeced result from statisticsV2Processor when updating statsV2:" << result;
            throw std::runtime_error("Unexpected state from statisticsV2Processor when updating statsV2");
    }

}

bool CreateWithdrawalRequestOpFrame::exceedsLowerBound(Database& db, AssetCode& code)
{
    xdr::xstring<256> key = "WithdrawLowerBound:" + code;
    auto lowerBound = KeyValueHelperLegacy::Instance()->loadKeyValue(key, db);
    if (!lowerBound) {
        return true;
    }

    if (lowerBound.get()->getKeyValue().value.type() != KeyValueEntryType::UINT64) {
        CLOG(WARNING, "WithdrawLowerBound")
            << "AssetCode:" << code
            << "KeyValueEntryType: "
            << std::to_string(
                static_cast<int32>(lowerBound.get()->getKeyValue().value.type()));
        return true;
    }

    auto &request = mCreateWithdrawalRequest.request;
    return lowerBound.get()->getKeyValue().value.ui64Value() <= request.amount;
}

}
