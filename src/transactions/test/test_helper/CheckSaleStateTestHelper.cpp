#include "CheckSaleStateTestHelper.h"
#include <transactions/FeesManager.h>
#include <ledger/AssetHelperLegacy.h>
#include <ledger/AccountHelper.h>
#include <ledger/BalanceHelperLegacy.h>
#include <ledger/OfferHelper.h>
#include <ledger/SaleAnteHelper.h>
#include "test/test_marshaler.h"
#include "ledger/SaleHelper.h"

namespace stellar
{
namespace txtest
{

void CheckSaleStateHelper::ensureCancel(uint64_t saleID, StateBeforeTxHelper& stateBeforeTx,
                                        std::unordered_map<BalanceID, SaleAnteFrame::pointer> saleAntesBeforeTx) const
{
    // asset unlocked
    const auto sale = stateBeforeTx.getSale(saleID);
    auto baseAssetBeforeTx = stateBeforeTx.getAssetEntry(sale->getBaseAsset());
    auto baseAssetAfterTx = AssetHelperLegacy::Instance()->loadAsset(sale->getBaseAsset(), mTestManager->getDB());

    // TODO: at current stage we do not allow to issue tokens before the sale. Must be fixed
    auto hardCapBaseAssetAmount = sale->getSaleEntry().maxAmountToBeSold;
    REQUIRE(baseAssetBeforeTx.pendingIssuance == baseAssetAfterTx->getPendingIssuance() + hardCapBaseAssetAmount);
    REQUIRE(baseAssetBeforeTx.availableForIssueance + hardCapBaseAssetAmount == baseAssetAfterTx->getAvailableForIssuance());

    // balances unlocked
    auto offers = stateBeforeTx.getAllOffers();
    if (!saleAntesBeforeTx.empty()) {
        REQUIRE(offers.size() == saleAntesBeforeTx.size());
    }
    for (auto offer : offers)
    {
        auto balanceBefore = stateBeforeTx.getBalance(offer.quoteBalance);
        REQUIRE(balanceBefore);
        auto balanceAfter = BalanceHelperLegacy::Instance()->mustLoadBalance(offer.quoteBalance, mTestManager->getDB());

        auto saleAnte = saleAntesBeforeTx[offer.quoteBalance];
        if (!!saleAnte) {
            REQUIRE(balanceBefore->getLocked() == balanceAfter->getLocked() + offer.quoteAmount + offer.fee +
                                                  saleAntesBeforeTx[balanceBefore->getBalanceID()]->getAmount());
        } else {
            REQUIRE(balanceBefore->getLocked() == balanceAfter->getLocked() + offer.quoteAmount + offer.fee);
        }
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
    StateBeforeTxHelper& stateBeforeTx, std::unordered_map<BalanceID, SaleAnteFrame::pointer> saleAntesBeforeTx) const
{
    auto sale = stateBeforeTx.getSale(result.saleID);
    auto baseAssetBeforeTx = stateBeforeTx.getAssetEntry(sale->getBaseAsset());
    auto baseAssetAfterTx = AssetHelperLegacy::Instance()->loadAsset(sale->getBaseAsset(), mTestManager->getDB());

    // always unlock hard cap
    if (sale->getSaleType() != SaleType::FIXED_PRICE)
    {
        auto hardCapBaseAsset = sale->getSaleEntry().maxAmountToBeSold;
        REQUIRE(baseAssetBeforeTx.pendingIssuance == baseAssetAfterTx->getPendingIssuance() + hardCapBaseAsset);
    }

    // check state of the asset
    auto issuedOnTheSale = baseAssetAfterTx->getIssued()- baseAssetBeforeTx.issued;
    auto expectedAvailableForIssuance = baseAssetBeforeTx.availableForIssueance + baseAssetBeforeTx.pendingIssuance - issuedOnTheSale;
    REQUIRE(baseAssetAfterTx->getAvailableForIssuance() + baseAssetAfterTx->getPendingIssuance() == expectedAvailableForIssuance);
    REQUIRE(baseAssetAfterTx->getIssued() - baseAssetBeforeTx.issued <= sale->getMaxAmountToBeSold());
    REQUIRE(baseAssetAfterTx->getMaxIssuanceAmount() == baseAssetAfterTx->getMaxIssuanceAmount());

    // check that sale owner have expected quote on balance
    for (const auto quoteAsset : sale->getSaleEntry().quoteAssets)
    {
        const auto quoteAssetResult = getOfferResultForQuoteBalance(result, quoteAsset.quoteBalance);
        checkBalancesAfterApproval(stateBeforeTx, sale, quoteAsset, quoteAssetResult, saleAntesBeforeTx);
    }

    auto baseBalanceBeforeTx = stateBeforeTx.getBalance(sale->getBaseBalanceID());
    auto baseBalanceAfterTx = BalanceHelperLegacy::Instance()->loadBalance(sale->getBaseBalanceID(), mTestManager->getDB());
    REQUIRE(baseBalanceBeforeTx->mEntry.data.balance() == baseBalanceAfterTx->mEntry.data.balance());
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

void CheckSaleStateHelper::ensureNoSaleAntesLeft(uint64_t saleID) const
{
    auto saleAntes = SaleAnteHelper::Instance()->loadSaleAntesForSale(saleID, mTestManager->getDB());
    REQUIRE(saleAntes.empty());
}

void CheckSaleStateHelper::checkBalancesAfterApproval(StateBeforeTxHelper& stateBeforeTx, SaleFrame::pointer sale,
                                                      SaleQuoteAsset const& saleQuoteAsset,
                                                      CheckSubSaleClosedResult result,
                                                      std::unordered_map<BalanceID, SaleAnteFrame::pointer> saleAntesBeforeTx) const
{
    auto ownerQuoteBalanceBefore = stateBeforeTx.getBalance(saleQuoteAsset.quoteBalance);
    REQUIRE(ownerQuoteBalanceBefore);
    auto ownerQuoteBalanceAfter = BalanceHelperLegacy::Instance()->mustLoadBalance(saleQuoteAsset.quoteBalance, mTestManager->getDB());
    auto ownerFrame = AccountHelper::Instance()->mustLoadAccount(sale->getOwnerID(), mTestManager->getDB());
    auto totalSellerFee = FeeManager::calculateCapitalDeploymentFeeForAccount(ownerFrame, saleQuoteAsset.quoteAsset, saleQuoteAsset.currentCap, mTestManager->getDB())
        .calculatedPercentFee;
    // TODO: currently it's possible to go a bit below currentCap
    REQUIRE(ownerQuoteBalanceAfter->getAmount() <= ownerQuoteBalanceBefore->getAmount() + saleQuoteAsset.currentCap - totalSellerFee);

    // check participants balances
    auto takenOffers = result.saleDetails.offersClaimed;
    uint64_t totalParticipantFee = 0;
    uint64_t totalSaleAnte = 0;
    for (auto& takenOffer : takenOffers)
    {
        // participant got his base asset
        auto baseBalanceBefore = stateBeforeTx.getBalance(takenOffer.baseBalance);
        REQUIRE(baseBalanceBefore);
        auto baseBalanceAfter = BalanceHelperLegacy::Instance()->mustLoadBalance(takenOffer.baseBalance, mTestManager->getDB());
        REQUIRE(baseBalanceAfter->getAmount() == baseBalanceBefore->getAmount() + takenOffer.baseAmount);

        // participant spent quote
        auto quoteBalanceBefore = stateBeforeTx.getBalance(takenOffer.quoteBalance);
        REQUIRE(quoteBalanceBefore);
        auto quoteBalanceAfter = BalanceHelperLegacy::Instance()->mustLoadBalance(takenOffer.quoteBalance, mTestManager->getDB());

        auto saleAnte = saleAntesBeforeTx[takenOffer.quoteBalance];

        auto proposedOffer = stateBeforeTx.getOffer(takenOffer.offerID, takenOffer.bAccountID);
        //unlock balance
        if (!!saleAnte) {
            REQUIRE(quoteBalanceBefore->getLocked() == quoteBalanceAfter->getLocked() + proposedOffer.quoteAmount +
                                                       proposedOffer.fee + saleAnte->getAmount());
            totalSaleAnte += saleAnte->getAmount();
        } else {
            REQUIRE(quoteBalanceBefore->getLocked() == quoteBalanceAfter->getLocked() + proposedOffer.quoteAmount +
                                                       proposedOffer.fee);
        }

        //change is available on balance
        int64_t change = proposedOffer.quoteAmount - takenOffer.quoteAmount;
        REQUIRE(quoteBalanceBefore->getAmount() + change == quoteBalanceAfter->getAmount());
        totalParticipantFee += takenOffer.bFeePaid;
    }

    // commission balance change
    auto commissionAfter = BalanceHelperLegacy::Instance()->loadBalance(mTestManager->getApp().getCommissionID(),
        saleQuoteAsset.quoteAsset, mTestManager->getDB(),
        nullptr);
    REQUIRE(commissionAfter);
    auto commissionBefore = stateBeforeTx.getBalance(commissionAfter->getBalanceID());
    REQUIRE(commissionBefore);

    if(!saleAntesBeforeTx.empty()){
        REQUIRE(commissionAfter->getAmount() == commissionBefore->getAmount() + totalParticipantFee + totalSellerFee +
                                                totalSaleAnte);
    } else {
        REQUIRE(commissionAfter->getAmount() == commissionBefore->getAmount() + totalParticipantFee + totalSellerFee);
    }
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
    auto saleAntesBeforeTx = SaleAnteHelper::Instance()->loadSaleAntes(saleID, mTestManager->getDB());

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
        ensureCancel(checkSaleStateResult.success().saleID, stateHelper, saleAntesBeforeTx);
        ensureNoOffersLeft(checkSaleStateResult.success(), stateHelper);
        ensureNoSaleAntesLeft(checkSaleStateResult.success().saleID);
        break;
    case CheckSaleStateEffect::CLOSED:
        ensureClose(checkSaleStateResult.success(), stateHelper, saleAntesBeforeTx);
        ensureNoOffersLeft(checkSaleStateResult.success(), stateHelper);
        ensureNoSaleAntesLeft(checkSaleStateResult.success().saleID);
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
