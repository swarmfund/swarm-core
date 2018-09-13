#include "util/asio.h"
#include "transactions/PayoutOpFrame.h"
#include "ledger/AccountHelper.h"
#include "ledger/AssetHelper.h"
#include "ledger/BalanceHelper.h"
#include "ledger/LedgerDelta.h"
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
        counterpartiesDetails) const
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

bool
PayoutOpFrame::processBalanceChange(Application &app,
                                    AccountManager::Result balanceChangeResult)
{
    if (balanceChangeResult == AccountManager::Result::UNDERFUNDED) {
        innerResult().code(PayoutResultCode::UNDERFUNDED);
        return false;
    }

    if (balanceChangeResult == AccountManager::Result::STATS_OVERFLOW) {
        innerResult().code(PayoutResultCode::STATS_OVERFLOW);
        return false;
    }

    if (balanceChangeResult == AccountManager::Result::LIMITS_EXCEEDED) {
        innerResult().code(PayoutResultCode::LIMITS_EXCEEDED);
        return false;
    }
    return true;
}

void
PayoutOpFrame::addShareAmount(BalanceFrame::pointer const &holder)
{
    uint64_t shareAmount = 0;

    if (!bigDivide(shareAmount,
                   static_cast<uint64_t>(holder->getAmount() + holder->getLocked()),
                   mPayout.maxPayoutAmount,
                   mAsset->getIssued(),
                   ROUND_DOWN)) {
        CLOG(ERROR, Logging::OPERATION_LOGGER)
                << "Unexpected state: share amount overflows UINT64_MAX, balance id: "
                << BalanceKeyUtils::toStrKey(holder->getBalanceID());
    }

    if (shareAmount > ONE) {
        mShareAmounts.emplace(holder->getAccountID(), shareAmount);
        mActualPayoutAmount += shareAmount;
    }
}

void
PayoutOpFrame::addReceiver(AccountID const &shareholderID, Database &db,
                           LedgerDelta &delta)
{
    auto receiverBalance = BalanceHelper::Instance()->loadBalance(shareholderID,
                                                                  mSourceBalance->getAsset(),
                                                                  db, &delta);
    if (!receiverBalance)
        receiverBalance = BalanceHelper::Instance()->createNewBalance(shareholderID, mSourceBalance->getAsset(),
                                                                      db, delta);

    mReceivers.emplace_back(receiverBalance);
}

uint64_t
PayoutOpFrame::getAssetHoldersTotalAmount(
        std::vector<BalanceFrame::pointer> holders)
{
    uint64_t totalAmount = 0;
    for (auto balance : holders)
    {
        if (balance->getAsset() != mPayout.asset)
        {
            CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected asset code, "
                   << "Expected: " << mPayout.asset
                   << "Actual: " << balance->getAsset();
            throw std::runtime_error("Unexpected asset code");
        }

        if (!safeSum(balance->getTotal(), totalAmount, totalAmount))
        {
            throw std::runtime_error("Unexpected state: "
                                     "total amount of issued tokens overflows");
        }
    }

    return totalAmount;
}

uint64_t
PayoutOpFrame::getHoldersAssetTotalAmount(AssetFrame::pointer assetFrame,
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
PayoutOpFrame::obtainAssetHoldersAmounts(uint64_t& totalAmount,
        std::vector<BalanceFrame::pointer> holders, uint64_t assetHoldersAmount)
{
    std::map<AccountID, uint64_t> result;
    totalAmount = 0;

    for (auto holder : holders)
    {
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

        //if (calculatedAmount == 0 && ) // fixme
        auto& amountToSend = result[holder->getAccountID()];
        amountToSend += calculatedAmount;
        totalAmount += calculatedAmount;
    }

    return result;
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
        const BalanceID balanceID = BalanceKeyUtils::forAccount(accountID,
                delta.getHeaderFrame().generateID(LedgerEntryType::BALANCE));
        auto newBalance = BalanceFrame::createNew(balanceID, accountID, mPayout.asset);

        if (!newBalance->tryFundAccount(assetHoldersAmounts[accountID]))
        {
            throw std::runtime_error("Unexpected state: can't fund new balance");
        }

        EntryHelperProvider::storeAddEntry(delta, db, newBalance->mEntry);
    }
}

bool
PayoutOpFrame::processTransfers(BalanceFrame::pointer sourceBalance, uint64_t totalAmount,
                std::map<AccountID, uint64_t> assetHoldersAmounts, Database db, LedgerDelta& delta)
{
    if (!sourceBalance->tryCharge(totalAmount))
    {
        innerResult().code(PayoutResultCode::UNDERFUNDED);//fix code
        return false;
    }

    std::vector<AccountID> accountIDs = getAccountIDs(assetHoldersAmounts);

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
            innerResult().code(PayoutResultCode::LINE_FULL);//fix code
            return false;
        }

        balanceHelper->storeChange(delta, db, receiverBalance->mEntry);
    }

    fundWithoutBalancesAccounts(accountIDs, assetHoldersAmounts, db, delta);

    balanceHelper->storeChange(delta, db, sourceBalance->mEntry);

    return true;
}

bool
PayoutOpFrame::tryProcessTransferFee(AccountManager& accountManager, BalanceFrame::pointer sourceBalance)
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

    accountManager.transferFee(sourceBalance->getAsset(), totalFee);

    return true;
}

bool
PayoutOpFrame::doApply(Application &app, LedgerDelta &delta,
                       LedgerManager &ledgerManager)
{
    Database &db = app.getDatabase();
    innerResult().code(PayoutResultCode::SUCCESS);

    auto assetFrame = AssetHelper::Instance()->loadAsset(mPayout.asset,
            getSourceID(), db);
    if (!assetFrame)
    {
        innerResult().code(PayoutResultCode::ASSET_NOT_FOUND);
        return false;
    }

    if (!assetFrame->isPolicySet(AssetPolicy::TRANSFERABLE))
    {
        innerResult().code(PayoutResultCode::NOT_ALLOWED_BY_ASSET_POLICY);//fix code
        return false;
    }

    auto sourceBalance = BalanceHelper::Instance()->loadBalance(getSourceID(),
            mPayout.sourceBalanceID, db, &delta);
    if (!sourceBalance)
    {
        innerResult().code(PayoutResultCode::BALANCE_NOT_FOUND);
        return false;
    }

    AccountManager accountManager(app, db, delta, ledgerManager);
    if (!tryProcessTransferFee(accountManager, sourceBalance))
        return false;

    auto holdersAssetAmount = getHoldersAssetTotalAmount(assetFrame, db);
    if (holdersAssetAmount == 0)
    {
        innerResult().code(PayoutResultCode::HOLDERS_NOT_FOUND);
        return false;
    }

    auto balanceHelper = BalanceHelper::Instance();
    auto assetHolderBalances = balanceHelper->loadAssetHolders(mPayout.asset,
            getSourceID(), db);
    if (assetHolderBalances.empty())
    {
        innerResult().code(PayoutResultCode::HOLDERS_NOT_FOUND);
        return false;
    }

    uint64_t actualTotalAmount = 0;
    auto assetHoldersAmounts = obtainAssetHoldersAmounts(actualTotalAmount, assetHolderBalances, holdersAssetAmount);

    if (actualTotalAmount == 0)
    {
        innerResult().code(PayoutResultCode::MIN_PAYOUT_AMOUNT_TOO_MUCH);
        return false;
    }

    if (processTransfers())

    for (auto const &holder : mHolders) {
        addShareAmount(holder);
    }

    innerResult().payoutSuccessResult().actualPayoutAmount = mActualPayoutAmount;

    for (auto const &shareAmount : mShareAmounts) {
        addReceiver(shareAmount.first, db, delta);
    }

    auto totalFee = mPayout.fee.percent + mPayout.fee.fixed;
    auto totalAmount = mActualPayoutAmount + totalFee;

    int64 sourceSentUniversal;
    auto transferResult = accountManager.processTransfer(mSourceAccount, mSourceBalance,
                                                         totalAmount, sourceSentUniversal);
    if (!processBalanceChange(app, transferResult))
        return false;
}

bool PayoutOpFrame::doCheckValid(Application &app) {
    if (mPayout.maxPayoutAmount == 0) {
        innerResult().code(PayoutResultCode::MALFORMED);
        return false;
    }

    if (!AssetFrame::isAssetCodeValid(mPayout.asset)) {
        innerResult().code(PayoutResultCode::MALFORMED);
        return false;
    }

    return true;
}

}