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
    return SourceDetails(getAllAccountTypes(),
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
    // if we do not have any fee frame - any fee is valid
    if (!feeFrame) {
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
    if (isSystemAccountType(mSourceAccount->getAccountType()))
        return (actualFee.fixed == 0) && (actualFee.percent == 0);

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
PayoutOpFrame::getAccountIDs(std::map<AccountID, uint64_t> assetHoldersAmounts)
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
PayoutOpFrame::obtainHoldersAmountsMap(Application& app, uint64_t& totalAmount,
        std::vector<BalanceFrame::pointer> holders, uint64_t assetHoldersAmount)
{
    std::map<AccountID, uint64_t> result;
    totalAmount = 0;
    auto systemAccounts = app.getSystemAccounts();

    for (auto holder : holders)
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

        if ((mPayout.minPayoutAmount != 0) &&
            (calculatedAmount < mPayout.minPayoutAmount))
            continue;

        auto& amountToSend = result[holder->getAccountID()];
        amountToSend += calculatedAmount;
        totalAmount += calculatedAmount;
    }

    return result;
}

void
PayoutOpFrame::addPayoutResponse(AccountID& accountID, uint64_t amount,
                                 BalanceID balanceID)
{
    PayoutResponse response;
    response.receivedAmount = amount;
    response.receiverBalanceID = balanceID;
    response.receiverID = accountID;
    innerResult().success().payoutResponses.emplace_back(response);
}

void
PayoutOpFrame::fundWithoutBalancesAccounts(std::vector<AccountID> accountIDs,
                            std::map<AccountID, uint64_t> assetHoldersAmounts,
                            AssetCode asset, StorageHelper& storageHelper)
{
    for (auto accountID : accountIDs)
    {
        // we don't check if there is balance for such accountID and asset code
        // because we already check it existing
        auto balanceID = BalanceKeyUtils::forAccount(accountID,
                storageHelper.getLedgerDelta().getHeaderFrame()
                        .generateID(LedgerEntryType::BALANCE));
        auto newBalance = BalanceFrame::createNew(balanceID, accountID, asset);

        if (!newBalance->tryFundAccount(assetHoldersAmounts[accountID]))
        {
            throw std::runtime_error("Unexpected state: can't fund new balance");
        }

        addPayoutResponse(accountID, assetHoldersAmounts[accountID],
                          newBalance->getBalanceID());

        auto& balanceHelper = storageHelper.getBalanceHelper();
        balanceHelper.storeAdd(newBalance->mEntry);
    }
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

    auto accountIDs = getAccountIDs(assetHoldersAmounts);

    auto& balanceHelper = storageHelper.getBalanceHelper();
    auto receiverBalances = balanceHelper.loadBalances(accountIDs,
            sourceBalance->getAsset());

    for (auto receiverBalance : receiverBalances)
    {
        auto accountID = receiverBalance->getAccountID();
        auto accountIDPos = std::find(accountIDs.begin(), accountIDs.end(),
                        accountID);
        if (accountIDPos == accountIDs.end())
        {
            CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected state: "
                       << "Expected accountID to be in vector. "
                       << "accountID: " + PubKeyUtils::toStrKey(accountID)
                       << "balanceID: " + BalanceKeyUtils::toStrKey(
                               receiverBalance->getBalanceID());
            throw std::runtime_error("Unexpected state. "
                                     "Expected accountID to be in vector.");
        }

        accountIDs.erase(accountIDPos);

        if (assetHoldersAmounts[accountID] == 0)
            continue;

        if (!receiverBalance->tryFundAccount(assetHoldersAmounts[accountID]))
        {
            innerResult().code(PayoutResultCode::LINE_FULL);
            return false;
        }

        addPayoutResponse(accountID, assetHoldersAmounts[accountID],
                          receiverBalance->getBalanceID());

        balanceHelper.storeChange(receiverBalance->mEntry);
    }

    fundWithoutBalancesAccounts(accountIDs, assetHoldersAmounts,
                                sourceBalance->getAsset(), storageHelper);

    balanceHelper.storeChange(sourceBalance->mEntry);
    innerResult().success().actualPayoutAmount = totalAmount;

    return true;
}

AssetFrame::pointer
PayoutOpFrame::obtainAsset(AssetHelper& assetHelper)
{
    auto assetFrame = assetHelper.loadAsset(mPayout.asset, getSourceID());
    if (assetFrame == nullptr)
    {
        innerResult().code(PayoutResultCode::ASSET_NOT_FOUND);
        return nullptr;
    }

    if (!assetFrame->isPolicySet(AssetPolicy::TRANSFERABLE))
    {
        innerResult().code(PayoutResultCode::ASSET_NOT_TRANSFERABLE);
        return nullptr;
    }

    return assetFrame;
}

std::vector<BalanceFrame::pointer>
PayoutOpFrame::obtainAssetHoldersBalances(uint64_t& issuedAmount,
                                          AssetFrame::pointer assetFrame,
                                          BalanceHelper& balanceHelper)
{
    issuedAmount = assetFrame->getIssued();
    if (issuedAmount == 0)
    {
        return std::vector<BalanceFrame::pointer>{};
    }

    return balanceHelper.loadAssetHolders(mPayout.asset,
                       getSourceID(), mPayout.minAssetHolderAmount);
}

bool
PayoutOpFrame::doApply(Application &app, StorageHelper &storageHelper,
                       LedgerManager &ledgerManager)
{
    innerResult().code(PayoutResultCode::SUCCESS);
    auto& balanceHelper = storageHelper.getBalanceHelper();
    auto& assetHelper = storageHelper.getAssetHelper();

    auto assetFrame = obtainAsset(assetHelper);
    if (assetFrame == nullptr)
        return false;

    auto sourceBalance = balanceHelper.loadBalance(mPayout.sourceBalanceID,
            getSourceID());
    if (sourceBalance == nullptr)
    {
        innerResult().code(PayoutResultCode::BALANCE_NOT_FOUND);
        return false;
    }

    uint64_t assetHoldersAmount;
    auto assetHoldersBalances = obtainAssetHoldersBalances(assetHoldersAmount,
            assetFrame, balanceHelper);
    if (assetHoldersBalances.empty())
    {
        innerResult().code(PayoutResultCode::HOLDERS_NOT_FOUND);
        return false;
    }

    uint64_t actualTotalAmount;
    auto holdersAmountsMap = obtainHoldersAmountsMap(app, actualTotalAmount,
            assetHoldersBalances, assetHoldersAmount);
    if (actualTotalAmount == 0)
    {
        innerResult().code(PayoutResultCode::MIN_AMOUNT_TOO_MUCH);
        return false;
    }

    Database& db = storageHelper.getDatabase();
    LedgerDelta& delta = storageHelper.getLedgerDelta();
    AccountManager accountManager(app, db, delta, ledgerManager);
    if (!tryProcessTransferFee(accountManager, db, actualTotalAmount,
                               sourceBalance))
        return false;

    if (!processTransfers(sourceBalance, actualTotalAmount, holdersAmountsMap,
                          storageHelper))
        return false;

    StatisticsV2Processor statsProcessor(db, delta, ledgerManager);
    return processStatistics(statsProcessor, sourceBalance, actualTotalAmount);
}

bool
PayoutOpFrame::processStatistics(StatisticsV2Processor statisticsV2Processor,
                                 BalanceFrame::pointer sourceBalance,
                                 uint64_t amount)
{
    uint64_t universalAmount;
    auto statisticsResult = statisticsV2Processor.addStatsV2(
            StatisticsV2Processor::PAYOUT, amount, universalAmount,
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