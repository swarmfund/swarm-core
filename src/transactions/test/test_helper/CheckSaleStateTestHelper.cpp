#include <ledger/BalanceHelper.h>
#include <ledger/AccountHelper.h>
#include <transactions/FeesManager.h>
#include "CheckSaleStateTestHelper.h"
#include "ledger/OfferHelper.h"
#include "ledger/AssetHelper.h"
#include "test/test_marshaler.h"

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

void CheckSaleStateHelper::ensureCancel(const CheckSaleStateSuccess result,
                                        StateBeforeTxHelper& stateBeforeTx) const
{
    // asset unlocked
    const auto sale = stateBeforeTx.getSale(result.saleID);
    auto baseAssetBeforeTx = stateBeforeTx.getAssetEntry(sale->getBaseAsset());
    auto baseAssetAfterTx = AssetHelper::Instance()->loadAsset(sale->getBaseAsset(), mTestManager->getDB());

    auto hardCapBaseAssetAmount = sale->getBaseAmountForHardCap();
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

void CheckSaleStateHelper::ensureClose(const CheckSaleStateSuccess result,
    StateBeforeTxHelper& stateBeforeTx) const
{
    const auto sale = stateBeforeTx.getSale(result.saleID);
    auto baseAssetBeforeTx = stateBeforeTx.getAssetEntry(sale->getBaseAsset());
    auto baseAssetAfterTx = AssetHelper::Instance()->loadAsset(sale->getBaseAsset(), mTestManager->getDB());

    // always unlock hard cap
    auto hardCapBaseAsset = sale->getBaseAmountForHardCap();
    REQUIRE(baseAssetBeforeTx.pendingIssuance == baseAssetAfterTx->getPendingIssuance() + hardCapBaseAsset);

    // can't issue after sale is closed
    auto currentCupBaseAsset = sale->getBaseAmountForCurrentCap();
    REQUIRE(baseAssetAfterTx->getAvailableForIssuance() == 0);
    REQUIRE(baseAssetAfterTx->getIssued() == baseAssetBeforeTx.issued + currentCupBaseAsset);
    REQUIRE(baseAssetAfterTx->getMaxIssuanceAmount() == baseAssetBeforeTx.issued + currentCupBaseAsset);

    // check that sale owner have expected quote on balance
    auto ownerQuoteBalanceBefore = stateBeforeTx.getBalance(sale->getQuoteBalanceID());
    REQUIRE(ownerQuoteBalanceBefore);
    auto ownerQuoteBalanceAfter = BalanceHelper::Instance()->mustLoadBalance(sale->getQuoteBalanceID(), mTestManager->getDB());
    auto ownerFrame = AccountHelper::Instance()->mustLoadAccount(sale->getOwnerID(), mTestManager->getDB());
    auto totalSellerFee = FeeManager::calculateOfferFeeForAccount(ownerFrame, sale->getQuoteAsset(), sale->getCurrentCap(), mTestManager->getDB())
                              .calculatedPercentFee;
    REQUIRE(ownerQuoteBalanceAfter->getAmount() == ownerQuoteBalanceBefore->getAmount() + sale->getCurrentCap() - totalSellerFee);

    // check participants balances
    auto takenOffers = result.effect.saleClosed().saleDetails.offersClaimed;
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
                                                                   sale->getQuoteAsset(), mTestManager->getDB(),
                                                                   nullptr);
    REQUIRE(commissionAfter);
    auto commissionBefore = stateBeforeTx.getBalance(commissionAfter->getBalanceID());
    REQUIRE(commissionBefore);

    REQUIRE(commissionAfter->getAmount() == commissionBefore->getAmount() + totalParticipantFee + totalSellerFee);
}

void CheckSaleStateHelper::ensureNoOffersLeft(CheckSaleStateSuccess result, StateBeforeTxHelper& stateBeforeTx) const
{
    const auto saleBeforeTx = stateBeforeTx.getSale(result.saleID);
    auto offers = OfferHelper::Instance()->loadOffersWithFilters(saleBeforeTx->getBaseAsset(), saleBeforeTx->getQuoteAsset(),
        &result.saleID, nullptr, mTestManager->getDB());
    REQUIRE(offers.empty());
}

CheckSaleStateHelper::CheckSaleStateHelper(const TestManager::pointer testManager) : TxHelper(testManager)
{

}

TransactionFramePtr CheckSaleStateHelper::createCheckSaleStateTx(Account& source)
{
    Operation op;
    op.body.type(OperationType::CHECK_SALE_STATE);
    return txFromOperation(source, op);
}

CheckSaleStateResult CheckSaleStateHelper::applyCheckSaleStateTx(
    Account& source, CheckSaleStateResultCode expectedResult)
{
    auto tx = createCheckSaleStateTx(source);
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
    ensureNoOffersLeft(checkSaleStateResult.success(), stateHelper);

    switch(checkSaleStateResult.success().effect.effect())
    {
    case CheckSaleStateEffect::CANCELED:
        ensureCancel(checkSaleStateResult.success(), stateHelper);
        break;
    case CheckSaleStateEffect::CLOSED:
        ensureClose(checkSaleStateResult.success(), stateHelper);
        break;
    default:
        throw std::runtime_error("Unexpected effect for check sale state");
    }

    return checkSaleStateResult;
}
}
}
