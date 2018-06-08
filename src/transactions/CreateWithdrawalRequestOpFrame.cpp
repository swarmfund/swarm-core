// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include <ledger/StatisticsHelper.h>
#include "transactions/CreateWithdrawalRequestOpFrame.h"
#include "database/Database.h"
#include "main/Application.h"
#include "medida/metrics_registry.h"
#include "ledger/LedgerDelta.h"
#include "ledger/AccountHelper.h"
#include "ledger/BalanceHelper.h"
#include "ledger/AssetHelper.h"
#include "ledger/AssetPairHelper.h"
#include "ledger/ReviewableRequestFrame.h"
#include "xdrpp/printer.h"
#include "ledger/ReviewableRequestHelper.h"

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
                             AccountType::ACCREDITED_INVESTOR, AccountType::INSTITUTIONAL_INVESTOR
                         }, mSourceAccount->getMediumThreshold(),
                         static_cast<int32_t>(SignerType::BALANCE_MANAGER),
                         static_cast<int32_t>(BlockReasons::TOO_MANY_KYC_UPDATE_REQUESTS));
}

BalanceFrame::pointer CreateWithdrawalRequestOpFrame::tryLoadBalance(
    Database& db, LedgerDelta& delta) const
{
    auto balanceFrame = BalanceHelper::Instance()->loadBalance(mCreateWithdrawalRequest.request.balance, db, &delta);
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


ReviewableRequestFrame::pointer CreateWithdrawalRequestOpFrame::createRequest(LedgerDelta& delta, LedgerManager& ledgerManager,
    Database& db, const AssetFrame::pointer assetFrame, const uint64_t universalAmount)
{
    auto request = ReviewableRequestFrame::createNew(delta, getSourceID(), assetFrame->getOwner(), nullptr,
                                                ledgerManager.getCloseTime());
    ReviewableRequestEntry& requestEntry = request->getRequestEntry();
    if (assetFrame->isPolicySet(AssetPolicy::TWO_STEP_WITHDRAWAL))
    {
        requestEntry.body.type(ReviewableRequestType::TWO_STEP_WITHDRAWAL);
        requestEntry.body.twoStepWithdrawalRequest() = mCreateWithdrawalRequest.request;
        requestEntry.body.twoStepWithdrawalRequest().universalAmount = universalAmount;
    } else
    {
        requestEntry.body.type(ReviewableRequestType::WITHDRAW);
        requestEntry.body.withdrawalRequest() = mCreateWithdrawalRequest.request;
        requestEntry.body.withdrawalRequest().universalAmount = universalAmount;
    }

    request->recalculateHashRejectReason();
    ReviewableRequestHelper::Instance()->storeAdd(delta, db, request->mEntry);
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

    const auto assetFrame = AssetHelper::Instance()->mustLoadAsset(balanceFrame->getAsset(), db);
    if (!assetFrame->isPolicySet(AssetPolicy::WITHDRAWABLE))
    {
        innerResult().code(CreateWithdrawalRequestResultCode::ASSET_IS_NOT_WITHDRAWABLE);
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

    uint64_t universalAmount = 0;
    const uint64_t amountToAdd = mCreateWithdrawalRequest.request.amount;
    if (!tryAddStats(accountManager, balanceFrame, amountToAdd, universalAmount))
        return false;

    BalanceHelper::Instance()->storeChange(delta, db, balanceFrame->mEntry);

    const ReviewableRequestFrame::pointer request = createRequest(delta, ledgerManager, db, assetFrame, universalAmount);
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


}
