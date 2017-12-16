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

SourceDetails CreateWithdrawalRequestOpFrame::getSourceAccountDetails(
    std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails)
const
{
    return SourceDetails({
                             AccountType::GENERAL, AccountType::SYNDICATE,
                             AccountType::OPERATIONAL
                         }, mSourceAccount->getMediumThreshold(),
                         static_cast<int32_t>(SignerType::BALANCE_MANAGER));
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
    if (!assetFrame->checkPolicy(AssetPolicy::WITHDRAWABLE))
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
    uint64_t amountToAdd = mCreateWithdrawalRequest.request.amount;
    if (!tryAddStats(accountManager, balanceFrame, amountToAdd, universalAmount))
        return false;

    BalanceHelper::Instance()->storeChange(delta, db, balanceFrame->mEntry);

    auto request = ReviewableRequestFrame::createNew(delta, getSourceID(), assetFrame->getOwner(), nullptr,
                                                     ledgerManager.getCloseTime());
    ReviewableRequestEntry& requestEntry = request->getRequestEntry();
    requestEntry.body.type(ReviewableRequestType::WITHDRAW);
    requestEntry.body.withdrawalRequest() = mCreateWithdrawalRequest.request;
    requestEntry.body.withdrawalRequest().universalAmount = universalAmount;
    request->recalculateHashRejectReason();
    ReviewableRequestHelper::Instance()->storeAdd(delta, db, request->mEntry);

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

    if (mCreateWithdrawalRequest.request.externalDetails.size() > app.
        getWithdrawalDetailsMaxLength())
    {
        innerResult().code(CreateWithdrawalRequestResultCode::INVALID_EXTERNAL_DETAILS);
        return false;
    }

    if (mCreateWithdrawalRequest.request.universalAmount != 0)
    {
        innerResult().code(CreateWithdrawalRequestResultCode::INVALID_UNIVERSAL_AMOUNT);
        return false;
    }

    return true;
}

bool CreateWithdrawalRequestOpFrame::tryAddStats(AccountManager& accountManager, BalanceFrame::pointer balance,
                                                 uint64_t amountToAdd, uint64_t& universalAmount)
{
    auto result = accountManager.addStats(mSourceAccount, balance, amountToAdd, universalAmount);
    switch (result) {
        case AccountManager::SUCCESS:
            return true;
            break;
        case AccountManager::STATS_OVERFLOW:
            innerResult().code(CreateWithdrawalRequestResultCode::STATS_OVERFLOW);
            false;
            break;
        case AccountManager::LIMITS_EXCEEDED:
            innerResult().code(CreateWithdrawalRequestResultCode::LIMITS_EXCEEDED);
            false;
            break;
        default:
            CLOG(ERROR, "CreateWithdrawalRequestOp") << "Unexpeced result from accountManager when updating stats";
            throw std::runtime_error("Unexpected state from accountManager when updating stats");
    }
}
}
