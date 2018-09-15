#include "util/asio.h"
#include "transactions/PayoutOpFrame.h"
#include "ledger/AccountHelper.h"
#include "ledger/AssetHelper.h"
#include "ledger/BalanceHelper.h"
#include "ledger/LedgerDelta.h"
#include <ledger/LedgerHeaderFrame.h>
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
    return SourceDetails({AccountType::SYNDICATE},
                         mSourceAccount->getMediumThreshold(),
                         static_cast<int32_t>(SignerType::ASSET_MANAGER));
}

bool
PayoutOpFrame::isFeeMatches(AccountManager &accountManager,
                            BalanceFrame::pointer balance) const
{
    return accountManager.isFeeMatches(mSourceAccount, mPayout.fee,
                                       FeeType::PAYOUT_FEE,
                                       FeeFrame::SUBTYPE_ANY,
                                       balance->getAsset(),
                                       mPayout.maxPayoutAmount);
}


uint64_t
PayoutOpFrame::obtainAssetHoldersTotalAmount(AssetFrame::pointer assetFrame,
                                             Database& db)
{
    auto totalAmount = assetFrame->getIssued();

    auto sourceBalances = BalanceHelper::Instance()->
            loadBalances(getSourceID(), mPayout.asset, db);

    for (auto balance : sourceBalances)
    {
        if (balance->getAmount() > totalAmount)
        {
            CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected total amount:"
                       << " Expected to be more than " << balance->getAmount()
                       << " Actual: " << totalAmount;
            throw std::runtime_error("Unexpected total amount");
        }

        totalAmount -= balance->getAmount();
    }

    return totalAmount;
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
    innerResult().payoutSuccessResult().payoutResponses.emplace_back(response);
}

void
PayoutOpFrame::fundWithoutBalancesAccounts(std::vector<AccountID> accountIDs,
                                           std::map<AccountID, uint64_t> assetHoldersAmounts,
                                           Database& db, LedgerDelta& delta)
{
    for (auto accountID : accountIDs)
    {
        // we don't check if there is balance for such accountID and asset code
        // because we already check it existing
        auto balanceID = BalanceKeyUtils::forAccount(accountID,
                delta.getHeaderFrame().generateID(LedgerEntryType::BALANCE));
        auto newBalance = BalanceFrame::createNew(balanceID, accountID,
                mPayout.asset);

        if (!newBalance->tryFundAccount(assetHoldersAmounts[accountID]))
        {
            throw std::runtime_error("Unexpected state: can't fund new balance");
        }

        addPayoutResponse(accountID, assetHoldersAmounts[accountID],
                          newBalance->getBalanceID());

        EntryHelperProvider::storeAddEntry(delta, db, newBalance->mEntry);
    }
}

bool
PayoutOpFrame::processTransfers(BalanceFrame::pointer sourceBalance,
        uint64_t totalAmount, std::map<AccountID, uint64_t> assetHoldersAmounts,
        Database& db, LedgerDelta& delta)
{
    if (!sourceBalance->tryCharge(totalAmount))
    {
        innerResult().code(PayoutResultCode::UNDERFUNDED);
        return false;
    }

    auto accountIDs = getAccountIDs(assetHoldersAmounts);

    auto balanceHelper = BalanceHelper::Instance();
    auto receiverBalances = balanceHelper->loadBalances(accountIDs,
            sourceBalance->getAsset(), db);

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

        balanceHelper->storeChange(delta, db, receiverBalance->mEntry);
    }

    fundWithoutBalancesAccounts(accountIDs, assetHoldersAmounts, db, delta);

    balanceHelper->storeChange(delta, db, sourceBalance->mEntry);

    return true;
}

bool
PayoutOpFrame::tryProcessTransferFee(AccountManager& accountManager,
                                     uint64_t actualTotalAmount,
                                     BalanceFrame::pointer sourceBalance)
{
    if (!isFeeMatches(accountManager, sourceBalance))
    {
        innerResult().code(PayoutResultCode::FEE_MISMATCHED);
        return false;
    }

    uint64_t totalFee = 0;
    if (!safeSum(mPayout.fee.percent, mPayout.fee.fixed, totalFee))
    {
        innerResult().code(PayoutResultCode::TOTAL_FEE_OVERFLOW);
        return false;
    }

    if (totalFee == 0)
        return true;

    if (!sourceBalance->tryCharge(totalFee))
    {
        innerResult().code(PayoutResultCode::UNDERFUNDED);
        return false;
    }

    accountManager.transferFee(sourceBalance->getAsset(), totalFee);

    return true;
}

AssetFrame::pointer
PayoutOpFrame::obtainAsset(Database& db)
{
    auto assetFrame = AssetHelper::Instance()->loadAsset(mPayout.asset,
                                                         getSourceID(), db);
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
PayoutOpFrame::obtainAssetHoldersBalances(uint64_t& assetHoldersAmount,
                                          AssetFrame::pointer assetFrame,
                                          Database& db)
{
    assetHoldersAmount = obtainAssetHoldersTotalAmount(assetFrame, db);
    if (assetHoldersAmount == 0)
    {
        return std::vector<BalanceFrame::pointer>{};
    }

    return BalanceHelper::Instance()->loadAssetHolders(mPayout.asset,
                                                       getSourceID(), db);
}

bool
PayoutOpFrame::doApply(Application &app, LedgerDelta &delta,
                       LedgerManager &ledgerManager)
{
    Database &db = app.getDatabase();
    innerResult().code(PayoutResultCode::SUCCESS);

    auto assetFrame = obtainAsset(db);
    if (assetFrame == nullptr)
        return false;

    auto sourceBalance = BalanceHelper::Instance()->loadBalance(getSourceID(),
            mPayout.sourceBalanceID, db, &delta);
    if (!sourceBalance)
    {
        innerResult().code(PayoutResultCode::BALANCE_NOT_FOUND);
        return false;
    }

    uint64_t assetHoldersAmount;
    auto assetHoldersBalances = obtainAssetHoldersBalances(assetHoldersAmount,
            assetFrame, db);
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
        innerResult().code(PayoutResultCode::MIN_PAYOUT_AMOUNT_TOO_MUCH);
        return false;
    }

    AccountManager accountManager(app, db, delta, ledgerManager);
    if (!tryProcessTransferFee(accountManager, sourceBalance))
        return false;

    innerResult().payoutSuccessResult().actualPayoutAmount = actualTotalAmount;

    if (!processTransfers(sourceBalance, actualTotalAmount, holdersAmountsMap, db, delta))
        return false;

    return processStatistics(accountManager, sourceBalance, actualTotalAmount);
}

bool
PayoutOpFrame::processStatistics(AccountManager accountManager,
                                 BalanceFrame::pointer sourceBalance,
                                 uint64_t amount)
{
    uint64_t universalAmount;
    // fixme, use statistics processor or fix account manager
    auto statisticsResult = accountManager.processStatistics(mSourceAccount,
            sourceBalance, amount, universalAmount);

    switch (statisticsResult.result)
    {
        case AccountManager::Result::SUCCESS:
            return true;
        case AccountManager::Result::STATS_OVERFLOW:
            innerResult().code(PayoutResultCode::STATS_OVERFLOW);
            return false;
        case AccountManager::Result::LIMITS_EXCEEDED:
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