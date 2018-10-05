#include "util/asio.h"
#include "transactions/PayoutOpFrame.h"
#include "ledger/AssetHelper.h"
#include "ledger/BalanceHelper.h"
#include "ledger/LedgerDelta.h"
#include <ledger/LedgerHeaderFrame.h>
#include "ledger/StorageHelper.h"
#include <ledger/FeeHelper.h>
#include "main/Application.h"

namespace stellar {
using xdr::operator==;

PayoutOpFrame::PayoutOpFrame(Operation const &op, OperationResult &res,
                             TransactionFrame &parentTx)
        : OperationFrame(op, res, parentTx), mPayout(mOperation.body.payoutOp())
{
}

std::unordered_map<AccountID, CounterpartyDetails>
PayoutOpFrame::getCounterpartyDetails(Database &db, LedgerDelta *delta) const
{
    // source account is only counterparty
    return {};
}

SourceDetails
PayoutOpFrame::getSourceAccountDetails(
        std::unordered_map<AccountID, CounterpartyDetails>
        counterpartiesDetails, int32_t ledgerVersion) const
{
    return SourceDetails({AccountType::MASTER, AccountType::SYNDICATE},
                         mSourceAccount->getHighThreshold(),
                         static_cast<int32_t>(SignerType::BALANCE_MANAGER));
}

Fee
PayoutOpFrame::getActualFee(AssetCode const& asset, uint64_t amount,
                            Database& db)
{
    Fee actualFee;
    actualFee.fixed = 0;
    actualFee.percent = 0;

    auto feeFrame = FeeHelper::Instance()->loadForAccount(FeeType::PAYOUT_FEE,
            asset, FeeFrame::SUBTYPE_ANY, mSourceAccount, amount, db);
    if (!feeFrame)
    {
        return actualFee;
    }

    if (!feeFrame->calculatePercentFee(amount, actualFee.percent, ROUND_UP))
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER)
                << "Actual calculated payout fee overflows, asset code: "
                << feeFrame->getFeeAsset();
        throw std::runtime_error("Actual calculated payout fee overflows");
    }

    actualFee.fixed = static_cast<uint64_t>(feeFrame->getFixedFee());
    return actualFee;
}

bool
PayoutOpFrame::isFeeAppropriate(Fee const& actualFee) const
{
    return (mPayout.fee.fixed >= actualFee.fixed) &&
           (mPayout.fee.percent >= actualFee.percent);
}

bool
PayoutOpFrame::tryProcessTransferFee(AccountManager& accountManager,
                                     Database& db, uint64_t actualTotalAmount,
                                     BalanceFrame::pointer sourceBalance)
{
    auto actualFee = getActualFee(sourceBalance->getAsset(),
                                  actualTotalAmount, db);

    if (!isFeeAppropriate(actualFee))
    {
        innerResult().code(PayoutResultCode::INSUFFICIENT_FEE_AMOUNT);
        return false;
    }

    uint64_t totalFee = 0;
    if (!safeSum(actualFee.percent, actualFee.fixed, totalFee))
    {
        innerResult().code(PayoutResultCode::TOTAL_FEE_OVERFLOW);
        return false;
    }

    if (totalFee == 0)
        return true;

    if (actualTotalAmount < totalFee)
    {
        innerResult().code(PayoutResultCode::FEE_EXCEEDS_ACTUAL_AMOUNT);
        return false;
    }

    if (!sourceBalance->tryCharge(totalFee))
    {
        innerResult().code(PayoutResultCode::UNDERFUNDED);
        return false;
    }

    accountManager.transferFee(sourceBalance->getAsset(), totalFee);
    innerResult().success().actualFee = actualFee;

    return true;
}

std::vector<AccountID>
PayoutOpFrame::getAccountIDs(std::map<AccountID, uint64_t>& assetHoldersAmounts)
{
    std::vector<AccountID> result;

    for (auto holderAmount : assetHoldersAmounts)
    {
        if (holderAmount.second == 0)
            continue;

        result.emplace_back(holderAmount.first);
    }

    return result;
}

std::map<AccountID, uint64_t>
PayoutOpFrame::obtainHoldersPayoutAmountsMap(Application& app, uint64_t& totalAmount,
        std::vector<BalanceFrame::pointer> holders, uint64_t assetHoldersAmount)
{
    std::map<AccountID, uint64_t> result;
    totalAmount = 0;
    auto systemAccounts = app.getSystemAccounts();

    for (auto const& holder : holders)
    {
        auto systemAccountIter = std::find(systemAccounts.begin(),
                systemAccounts.end(), holder->getAccountID());
        if (systemAccountIter != systemAccounts.end())
            continue;

        uint64_t calculatedAmount;
        if (!bigDivide(calculatedAmount, mPayout.maxPayoutAmount,
                       holder->getTotal(), assetHoldersAmount, ROUND_DOWN))
        {
            CLOG(ERROR, Logging::OPERATION_LOGGER)
                << "Unexpected state: calculatedAmount overflows UINT64_MAX, "
                << "balance id: "
                << BalanceKeyUtils::toStrKey(holder->getBalanceID());
            throw std::runtime_error("Unexpected state: calculatedAmount "
                                     "overflows UINT64_MAX");
        }

        if ((calculatedAmount == 0) ||
            (calculatedAmount < mPayout.minPayoutAmount))
            continue;

        auto& amountToSend = result[holder->getAccountID()];
        if (!safeSum(amountToSend, calculatedAmount, amountToSend))
        {
            throw std::runtime_error("Unexpected state, amount to send overflows");
        }

        if (!safeSum(totalAmount, calculatedAmount, totalAmount))
        {
            throw std::runtime_error("Unexpected state, amount to send overflows");
        }
    }

    if (totalAmount > mPayout.maxPayoutAmount)
    {
        throw std::runtime_error("Unexpected total amount to be more than maxPayoutAmount");
    }

    return result;
}

void
PayoutOpFrame::addPayoutResponse(AccountID const& accountID, uint64_t amount,
                                 BalanceID const& balanceID)
{
    PayoutResponse response;
    response.receivedAmount = amount;
    response.receiverBalanceID = balanceID;
    response.receiverID = accountID;
    innerResult().success().payoutResponses.emplace_back(response);
}

std::map<AccountID, BalanceFrame::pointer>
PayoutOpFrame::obtainAccountIDBalanceMap(
                            std::map<AccountID, uint64_t>& assetHoldersAmounts,
                            AssetCode assetCode, BalanceHelper& balanceHelper)
{
    auto accountIDs = getAccountIDs(assetHoldersAmounts);

    auto receiverBalances = balanceHelper.loadBalances(accountIDs, assetCode);

    std::map<AccountID, BalanceFrame::pointer> result;
    for (auto balance : receiverBalances)
    {
        result.emplace(balance->getAccountID(), balance);
    }

    return result;
}

bool
PayoutOpFrame::processTransfers(BalanceFrame::pointer sourceBalance,
        uint64_t totalAmount, std::map<AccountID, uint64_t> assetHoldersAmounts,
        StorageHelper& storageHelper)
{
    if (!sourceBalance->tryCharge(totalAmount))
    {
        innerResult().code(PayoutResultCode::UNDERFUNDED);
        return false;
    }

    auto& balanceHelper = storageHelper.getBalanceHelper();
    auto accountIDBalanceMap = obtainAccountIDBalanceMap(assetHoldersAmounts,
            sourceBalance->getAsset(), balanceHelper);

    for (auto const& holdersAmount : assetHoldersAmounts)
    {
        if (holdersAmount.second == 0)
            continue;

        bool isNewBalance = false;
        auto receiverBalance = accountIDBalanceMap[holdersAmount.first];
        if (!receiverBalance)
        {
            auto balanceID = BalanceKeyUtils::forAccount(
                    holdersAmount.first, storageHelper.getLedgerDelta()
                        ->getHeaderFrame().generateID(LedgerEntryType::BALANCE));
            receiverBalance = BalanceFrame::createNew(balanceID,
                    holdersAmount.first, sourceBalance->getAsset());

            isNewBalance = true;
        }

        if (!receiverBalance->tryFundAccount(holdersAmount.second))
        {
            innerResult().code(PayoutResultCode::LINE_FULL);
            return false;
        }

        addPayoutResponse(holdersAmount.first, holdersAmount.second,
                          receiverBalance->getBalanceID());

        if (isNewBalance)
        {
            balanceHelper.storeAdd(receiverBalance->mEntry);
            continue;
        }

        balanceHelper.storeChange(receiverBalance->mEntry);
    }

    innerResult().success().actualPayoutAmount = totalAmount;

    return true;
}

BalanceFrame::pointer
PayoutOpFrame::obtainSourceBalance(BalanceHelper& balanceHelper,
                                   AssetHelper& assetHelper)
{
    auto sourceBalance = balanceHelper.loadBalance(mPayout.sourceBalanceID,
                                                   getSourceID());
    if (!sourceBalance)
    {
        innerResult().code(PayoutResultCode::BALANCE_NOT_FOUND);
        return nullptr;
    }

    auto asset = assetHelper.mustLoadAsset(sourceBalance->getAsset());

    if (!asset->isPolicySet(AssetPolicy::TRANSFERABLE))
    {
        innerResult().code(PayoutResultCode::ASSET_NOT_TRANSFERABLE);
        return nullptr;
    }

    return sourceBalance;
}

std::vector<BalanceFrame::pointer>
PayoutOpFrame::obtainAssetHoldersBalances(AssetFrame::pointer assetFrame,
                                          BalanceHelper& balanceHelper)
{
    if (assetFrame->getIssued() == 0)
    {
        return std::vector<BalanceFrame::pointer>{};
    }

    return balanceHelper.loadAssetHolders(mPayout.asset, getSourceID(),
                                          mPayout.minAssetHolderAmount);
}

bool
PayoutOpFrame::doApply(Application &app, StorageHelper &storageHelper,
                       LedgerManager &ledgerManager)
{
    innerResult().code(PayoutResultCode::SUCCESS);
    auto& balanceHelper = storageHelper.getBalanceHelper();
    auto& assetHelper = storageHelper.getAssetHelper();

    auto assetFrame = assetHelper.loadAsset(mPayout.asset, getSourceID());
    if (!assetFrame)
    {
        innerResult().code(PayoutResultCode::ASSET_NOT_FOUND);
        return false;
    }

    auto sourceBalance = obtainSourceBalance(balanceHelper, assetHelper);
    if (!sourceBalance)
        return false;

    auto assetHoldersBalances = obtainAssetHoldersBalances(assetFrame,
            balanceHelper);
    if (assetHoldersBalances.empty())
    {
        innerResult().code(PayoutResultCode::HOLDERS_NOT_FOUND);
        return false;
    }

    uint64_t actualTotalAmount;
    auto holdersAmountsMap = obtainHoldersPayoutAmountsMap(app, actualTotalAmount,
            assetHoldersBalances, assetFrame->getIssued());
    if (actualTotalAmount == 0)
    {
        innerResult().code(PayoutResultCode::MIN_AMOUNT_TOO_BIG);
        return false;
    }

    Database& db = storageHelper.getDatabase();
    LedgerDelta* delta = storageHelper.getLedgerDelta();
    AccountManager accountManager(app, db, *delta, ledgerManager);
    if (!tryProcessTransferFee(accountManager, db, actualTotalAmount,
                               sourceBalance))
        return false;

    if (!processTransfers(sourceBalance, actualTotalAmount, holdersAmountsMap,
                          storageHelper))
        return false;

    balanceHelper.storeChange(sourceBalance->mEntry);

    StatisticsV2Processor statsProcessor(db, *delta, ledgerManager);
    return processStatistics(statsProcessor, sourceBalance, actualTotalAmount);
}

bool
PayoutOpFrame::processStatistics(StatisticsV2Processor statisticsV2Processor,
                                 BalanceFrame::pointer sourceBalance,
                                 uint64_t amount)
{
    uint64_t universalAmount;
    auto statisticsResult = statisticsV2Processor.addStatsV2(
            StatisticsV2Processor::PAYMENT, amount, universalAmount,
            mSourceAccount, sourceBalance);

    switch (statisticsResult)
    {
        case StatisticsV2Processor::Result::SUCCESS:
            return true;
        case StatisticsV2Processor::Result::STATS_V2_OVERFLOW:
            innerResult().code(PayoutResultCode::STATS_OVERFLOW);
            return false;
        case StatisticsV2Processor::Result::LIMITS_V2_EXCEEDED:
            innerResult().code(PayoutResultCode::LIMITS_EXCEEDED);
            return false;
        default:
            CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected case "
                                                      "in process statistics";
            throw std::runtime_error("Unexpected case in process statistics");
    }
}

bool
PayoutOpFrame::doCheckValid(Application &app)
{
    if (mPayout.maxPayoutAmount == 0)
    {
        innerResult().code(PayoutResultCode::INVALID_AMOUNT);
        return false;
    }

    if (!AssetFrame::isAssetCodeValid(mPayout.asset))
    {
        innerResult().code(PayoutResultCode::INVALID_ASSET);
        return false;
    }

    return true;
}

std::string
PayoutOpFrame::getInnerResultCodeAsStr()
{
    const auto result = getResult();
    const auto code = getInnerCode(result);
    return xdr::xdr_traits<PayoutResultCode>::enum_name(code);
}

}