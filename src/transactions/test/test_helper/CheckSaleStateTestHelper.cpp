#include <ledger/BalanceHelper.h>
#include <ledger/AccountHelper.h>
#include <transactions/FeesManager.h>
#include "CheckSaleStateTestHelper.h"
#include "ledger/OfferHelper.h"
#include "ledger/AssetHelper.h"
#include "test/test_marshaler.h"
#include "ledger/SaleHelper.h"

namespace stellar
{
namespace txtest
{
StateBeforeTxHelper::StateBeforeTxHelper(const LedgerDelta::KeyEntryMap state)
{
    mState = state;
}

SaleFrame::pointer StateBeforeTxHelper::getSale(const uint64_t id)
{
    LedgerKey key;
    key.type(LedgerEntryType::SALE);
    key.sale().saleID = id;
    auto sale = mState.find(key);
    if (sale == mState.end())
    {
        return nullptr;
    }

    return std::make_shared<SaleFrame>(sale->second->mEntry);
}

AssetEntry StateBeforeTxHelper::getAssetEntry(AssetCode assetCode)
{
    LedgerKey key;
    key.type(LedgerEntryType::ASSET);
    key.asset().code = assetCode;
    return mState[key]->mEntry.data.asset();
}

BalanceFrame::pointer StateBeforeTxHelper::getBalance(BalanceID balanceID) {
    LedgerKey key;
    key.type(LedgerEntryType::BALANCE);
    key.balance().balanceID = balanceID;
    if (mState.find(key) == mState.end())
        return nullptr;
    return std::make_shared<BalanceFrame>(mState[key]->mEntry);
}

OfferEntry StateBeforeTxHelper::getOffer(uint64_t offerID, AccountID ownerID)
{
    LedgerKey key;
    key.type(LedgerEntryType::OFFER_ENTRY);
    key.offer().offerID = offerID;
    key.offer().ownerID = ownerID;
    return mState[key]->mEntry.data.offer();
}

std::vector<OfferEntry> StateBeforeTxHelper::getAllOffers()
{
    std::vector<OfferEntry> offers;
    for (auto entryPair : mState)
    {
        const auto& ledgerEntry = entryPair.second->mEntry;
        if (ledgerEntry.data.type() == LedgerEntryType::OFFER_ENTRY)
            offers.push_back(ledgerEntry.data.offer());
    }
    return offers;
}

AccountFrame::pointer StateBeforeTxHelper::getAccount(AccountID accountID)
{
    LedgerKey key;
    key.type(LedgerEntryType::ACCOUNT);
    key.account().accountID = accountID;

    if (mState.find(key) == mState.end())
        return nullptr;

    return std::make_shared<AccountFrame>(mState[key]->mEntry);
}

    void CheckSaleStateHelper::ensureCancel(uint64_t saleID,
                                        StateBeforeTxHelper &stateBeforeTx) const
{
    // asset unlocked
    const auto sale = stateBeforeTx.getSale(saleID);
    auto baseAssetBeforeTx = stateBeforeTx.getAssetEntry(sale->getBaseAsset());
    auto baseAssetAfterTx = AssetHelper::Instance()->loadAsset(sale->getBaseAsset(), mTestManager->getDB());

    // TODO: at current stage we do not allow to issue tokens before the sale. Must be fixed
    auto hardCapBaseAssetAmount = baseAssetBeforeTx.maxIssuanceAmount;
    REQUIRE(baseAssetBeforeTx.pendingIssuance == baseAssetAfterTx->getPendingIssuance() + hardCapBaseAssetAmount);
    REQUIRE(baseAssetBeforeTx.availableForIssueance + hardCapBaseAssetAmount == baseAssetAfterTx->getAvailableForIssuance());

    // balances unlocked
    auto offers = stateBeforeTx.getAllOffers();
    for (auto offer : offers)
    {
        auto balanceBefore = stateBeforeTx.getBalance(offer.quoteBalance);
        REQUIRE(balanceBefore);
        auto balanceAfter = BalanceHelper::Instance()->mustLoadBalance(offer.quoteBalance, mTestManager->getDB());
        REQUIRE(balanceBefore->getLocked() == balanceAfter->getLocked() + offer.quoteAmount + offer.fee);
    }
}

CheckSubSaleClosedResult getOfferResultForQuoteBalance(const CheckSaleStateSuccess result, BalanceID const& quoteBalanceID)
{
    for (auto assetResult : result.effect.saleClosed().results)
    {
        if (assetResult.saleQuoteBalance == quoteBalanceID)
        {
            return assetResult;
        }
    }

    throw std::runtime_error("Failed to find result for balance");
}
void CheckSaleStateHelper::ensureClose(const CheckSaleStateSuccess result,
    StateBeforeTxHelper& stateBeforeTx) const
{
    auto sale = stateBeforeTx.getSale(result.saleID);
    auto baseAssetBeforeTx = stateBeforeTx.getAssetEntry(sale->getBaseAsset());
    auto baseAssetAfterTx = AssetHelper::Instance()->loadAsset(sale->getBaseAsset(), mTestManager->getDB());

    // always unlock hard cap
    auto hardCapBaseAsset = baseAssetBeforeTx.maxIssuanceAmount;
    REQUIRE(baseAssetBeforeTx.pendingIssuance == baseAssetAfterTx->getPendingIssuance() + hardCapBaseAsset);

    // can't issue after sale is closed
    auto currentCupBaseAsset = std::min(sale->getBaseAmountForCurrentCap(), baseAssetBeforeTx.maxIssuanceAmount);
    REQUIRE(baseAssetAfterTx->getAvailableForIssuance() == 0);
    REQUIRE(baseAssetAfterTx->getIssued() == baseAssetBeforeTx.issued + currentCupBaseAsset);
    REQUIRE(baseAssetAfterTx->getMaxIssuanceAmount() == baseAssetBeforeTx.issued + currentCupBaseAsset);

    // check that sale owner have expected quote on balance
    for (const auto quoteAsset : sale->getSaleEntry().quoteAssets)
    {
        const auto quoteAssetResult = getOfferResultForQuoteBalance(result, quoteAsset.quoteBalance);
        checkBalancesAfterApproval(stateBeforeTx, sale, quoteAsset, quoteAssetResult);
    }
}

void CheckSaleStateHelper::ensureUpdated(const CheckSaleStateSuccess result,
    StateBeforeTxHelper& stateBeforeTx) const
{
    auto saleBeforeTx = stateBeforeTx.getSale(result.saleID);
    auto saleAfterTx = SaleHelper::Instance()->loadSale(result.saleID, mTestManager->getDB());
    REQUIRE(!!saleAfterTx);
    auto isUpdated = false;
    for (auto i = 0; i < saleBeforeTx->getSaleEntry().quoteAssets.size(); i++)
    {
        auto quoteAssetBeforeTx = saleBeforeTx->getSaleEntry().quoteAssets[i];
        auto quoteAssetAfterTx = saleAfterTx->getSaleEntry().quoteAssets[i];
        REQUIRE(quoteAssetBeforeTx.quoteAsset == quoteAssetAfterTx.quoteAsset);
        REQUIRE(quoteAssetBeforeTx.currentCap >= quoteAssetAfterTx.currentCap);
        if (quoteAssetBeforeTx.currentCap > quoteAssetAfterTx.currentCap)
        {
            isUpdated = true;
        }
    }

    REQUIRE(isUpdated);
}

void CheckSaleStateHelper::ensureNoOffersLeft(CheckSaleStateSuccess result, StateBeforeTxHelper& stateBeforeTx) const
{
    auto saleBeforeTx = stateBeforeTx.getSale(result.saleID);
    for (auto saleQuoteAsset : saleBeforeTx->getSaleEntry().quoteAssets)
    {
        auto offers = OfferHelper::Instance()->loadOffersWithFilters(saleBeforeTx->getBaseAsset(), saleQuoteAsset.quoteAsset,
            &result.saleID, nullptr, mTestManager->getDB());
        REQUIRE(offers.empty());
    }
}

void CheckSaleStateHelper::checkBalancesAfterApproval(StateBeforeTxHelper& stateBeforeTx, SaleFrame::pointer sale,
    SaleQuoteAsset const& saleQuoteAsset, CheckSubSaleClosedResult result) const
{
    auto ownerQuoteBalanceBefore = stateBeforeTx.getBalance(saleQuoteAsset.quoteBalance);
    REQUIRE(ownerQuoteBalanceBefore);
    auto ownerQuoteBalanceAfter = BalanceHelper::Instance()->mustLoadBalance(saleQuoteAsset.quoteBalance, mTestManager->getDB());
    auto ownerFrame = AccountHelper::Instance()->mustLoadAccount(sale->getOwnerID(), mTestManager->getDB());
    auto totalSellerFee = FeeManager::calculateOfferFeeForAccount(ownerFrame, saleQuoteAsset.quoteAsset, saleQuoteAsset.currentCap, mTestManager->getDB())
        .calculatedPercentFee;
    // TODO: currently it's possible to go a bit below currentCap
    REQUIRE(ownerQuoteBalanceAfter->getAmount() <= ownerQuoteBalanceBefore->getAmount() + saleQuoteAsset.currentCap - totalSellerFee);

    // check participants balances
    auto takenOffers = result.saleDetails.offersClaimed;
    uint64_t totalParticipantFee = 0;
    for (auto& takenOffer : takenOffers)
    {
        // participant got his base asset
        auto baseBalanceBefore = stateBeforeTx.getBalance(takenOffer.baseBalance);
        REQUIRE(baseBalanceBefore);
        auto baseBalanceAfter = BalanceHelper::Instance()->mustLoadBalance(takenOffer.baseBalance, mTestManager->getDB());
        REQUIRE(baseBalanceAfter->getAmount() == baseBalanceBefore->getAmount() + takenOffer.baseAmount);

        // participant spent quote
        auto quoteBalanceBefore = stateBeforeTx.getBalance(takenOffer.quoteBalance);
        REQUIRE(quoteBalanceBefore);
        auto quoteBalanceAfter = BalanceHelper::Instance()->mustLoadBalance(takenOffer.quoteBalance, mTestManager->getDB());

        auto proposedOffer = stateBeforeTx.getOffer(takenOffer.offerID, takenOffer.bAccountID);
        //unlock balance
        REQUIRE(quoteBalanceBefore->getLocked() == quoteBalanceAfter->getLocked() + proposedOffer.quoteAmount + proposedOffer.fee);
        //change is available on balance
        int64_t change = proposedOffer.quoteAmount - takenOffer.quoteAmount;
        REQUIRE(quoteBalanceBefore->getAmount() + change == quoteBalanceAfter->getAmount());
        totalParticipantFee += takenOffer.bFeePaid;
    }

    // commission balance change
    auto commissionAfter = BalanceHelper::Instance()->loadBalance(mTestManager->getApp().getCommissionID(),
        saleQuoteAsset.quoteAsset, mTestManager->getDB(),
        nullptr);
    REQUIRE(commissionAfter);
    auto commissionBefore = stateBeforeTx.getBalance(commissionAfter->getBalanceID());
    REQUIRE(commissionBefore);

    REQUIRE(commissionAfter->getAmount() == commissionBefore->getAmount() + totalParticipantFee + totalSellerFee);
}

CheckSaleStateHelper::CheckSaleStateHelper(const TestManager::pointer testManager) : TxHelper(testManager)
{

}

TransactionFramePtr CheckSaleStateHelper::createCheckSaleStateTx(Account& source, uint64_t saleID)
{
    Operation op;
    op.body.type(OperationType::CHECK_SALE_STATE);
    op.body.checkSaleStateOp().saleID = saleID;
    return txFromOperation(source, op);
}

CheckSaleStateResult CheckSaleStateHelper::applyCheckSaleStateTx(
    Account& source, uint64_t saleID, CheckSaleStateResultCode expectedResult)
{
    auto tx = createCheckSaleStateTx(source, saleID);
    std::vector<LedgerDelta::KeyEntryMap> stateBeforeOps;
    mTestManager->applyCheck(tx, stateBeforeOps);
    auto txResult = tx->getResult();
    const auto checkSaleStateResult = txResult.result.results()[0].tr().checkSaleStateResult();
    auto actualResultCode = checkSaleStateResult.code();
    REQUIRE(actualResultCode == expectedResult);


    if (actualResultCode != CheckSaleStateResultCode::SUCCESS)
    {
        return checkSaleStateResult;
    }

    REQUIRE(stateBeforeOps.size() == 1);
    const auto stateBeforeOp = stateBeforeOps[0];
    auto stateHelper = StateBeforeTxHelper(stateBeforeOp);
    const auto effect = checkSaleStateResult.success().effect.effect();
    switch(effect)
    {
    case CheckSaleStateEffect::CANCELED:
        ensureCancel(checkSaleStateResult.success().saleID, stateHelper);
        ensureNoOffersLeft(checkSaleStateResult.success(), stateHelper);
        break;
    case CheckSaleStateEffect::CLOSED:
        ensureClose(checkSaleStateResult.success(), stateHelper);
        ensureNoOffersLeft(checkSaleStateResult.success(), stateHelper);
        break;
    case CheckSaleStateEffect::UPDATED:
        ensureUpdated(checkSaleStateResult.success(), stateHelper);
        break;
    default:
        throw std::runtime_error("Unexpected effect for check sale state");
    }

    return checkSaleStateResult;
}
}
}
