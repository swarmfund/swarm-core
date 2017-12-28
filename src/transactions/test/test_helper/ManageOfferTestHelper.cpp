#include "ManageOfferTestHelper.h"
#include "transactions/test/TxTests.h"
#include "ledger/AssetPairHelper.h"
#include "ledger/OfferHelper.h"
#include "ledger/BalanceHelper.h"
#include "xdrpp/printer.h"
#include "transactions/dex/OfferManager.h"

namespace stellar
{
namespace txtest
{
void ManageOfferTestHelper::ensureAssetPairPriceUpdated(
    ManageOfferSuccessResult success, LedgerDelta::KeyEntryMap& stateBeforeTx) const
{
    auto assetPairAfterTx = AssetPairHelper::Instance()->loadAssetPair(success.baseAsset, success.quoteAsset, mTestManager->getDB());
    REQUIRE(!!assetPairAfterTx);
    if (success.offersClaimed.empty())
    {
        auto assetPairBeforeTx = stateBeforeTx.find(assetPairAfterTx->getKey());
        // ensure asset pair was not updated, as there is no matches
        if (assetPairBeforeTx == stateBeforeTx.end())
            return;
        REQUIRE(assetPairBeforeTx->second->mEntry.data.assetPair() == assetPairAfterTx->getAssetPair());
    }
    
    auto currentPrice = success.offersClaimed[success.offersClaimed.size() - 1].currentPrice;
    REQUIRE(assetPairAfterTx->getCurrentPrice() == currentPrice);
}

void ManageOfferTestHelper::ensureFeesPaidCorrectly(
    ManageOfferSuccessResult success,
    LedgerDelta::KeyEntryMap& stateBeforeTx) const
{
    auto feesBalanceAfterTx = BalanceHelper::Instance()->loadBalance(mTestManager->getApp().getCommissionID(), success.quoteAsset, mTestManager->getDB(), nullptr);
    auto feesBalanceBeforeTx = stateBeforeTx.find(feesBalanceAfterTx->getKey());
    if (success.offersClaimed.empty())
    {
        if (feesBalanceBeforeTx == stateBeforeTx.end())
            return;
        REQUIRE(feesBalanceAfterTx->getBalance() == feesBalanceBeforeTx->second->mEntry.data.balance());
    }

    uint64_t totalFeesPaid = 0;
    for (const auto claimedOffer : success.offersClaimed)
    {
        totalFeesPaid += claimedOffer.aFeePaid;
        totalFeesPaid += claimedOffer.bFeePaid;
    }

    REQUIRE(feesBalanceAfterTx->getAmount() == feesBalanceBeforeTx->second->mEntry.data.balance().amount + totalFeesPaid);
}

void ManageOfferTestHelper::ensureDeleteSuccess(Account& source, ManageOfferOp op,
                                                const ManageOfferSuccessResult success, LedgerDelta::KeyEntryMap& stateBeforeTx)
{
    ensureFeesPaidCorrectly(success, stateBeforeTx);
    ensureAssetPairPriceUpdated(success, stateBeforeTx);
}

void ManageOfferTestHelper::ensureCreateSuccess(Account& source, ManageOfferOp op,
    ManageOfferSuccessResult success, LedgerDelta::KeyEntryMap& stateBeforeTx)
{
    ensureFeesPaidCorrectly(success, stateBeforeTx);
    ensureAssetPairPriceUpdated(success, stateBeforeTx);
    auto& offerResult = success.offer;
    auto claimedOffers = success.offersClaimed;
    const auto expectedOfferID = op.offerID == 0 ? mTestManager->getLedgerDelta().getHeaderFrame().getLastGeneratedID(LedgerEntryType::OFFER_ENTRY) + 1 : op.offerID;

    auto offer = OfferHelper::Instance()->loadOffer(source.key.getPublicKey(), expectedOfferID, mTestManager->getDB());
    switch (offerResult.effect())
    {
    case ManageOfferEffect::CREATED:
    case ManageOfferEffect::UPDATED:
    {
        REQUIRE(!!offer);
        auto& offerEntry = offer->getOffer();
        REQUIRE(offerEntry == offerResult.offer());
        REQUIRE(offerEntry.price == op.price);
        REQUIRE(offerEntry.baseBalance == op.baseBalance);
        REQUIRE(offerEntry.quoteBalance == op.quoteBalance);
    }
    break;
    case ManageOfferEffect::DELETED:
        REQUIRE(!offer);
        break;
    default:
        throw std::runtime_error("Unexpected offer effect");
    }
}

ManageOfferTestHelper::ManageOfferTestHelper(const TestManager::pointer testManager) : TxHelper(testManager)
{
}

ManageOfferResult ManageOfferTestHelper::applyManageOffer(Account& source, const uint64_t offerID, BalanceID const& baseBalance,
    BalanceID const& quoteBalance, const int64_t amount, const int64_t price, const bool isBuy,
                                                              const int64_t fee, const ManageOfferResultCode expectedResult)
{
    auto manageOfferOp = OfferManager::buildManageOfferOp(baseBalance, quoteBalance, isBuy, amount, price, fee,
        offerID, ManageOfferOpFrame::SECONDARY_MARKET_ORDER_BOOK_ID);
    return applyManageOffer(source, manageOfferOp, expectedResult);
}

TransactionFramePtr ManageOfferTestHelper::creatManageOfferTx(Account& source,
   const uint64_t offerID, BalanceID const& baseBalance,
    BalanceID const& quoteBalance, const int64_t amount, const int64_t price, const bool isBuy,
                                                              const int64_t fee)
{
    Operation op;
    op.body.type(OperationType::MANAGE_OFFER);
    op.body.manageOfferOp().amount = amount;
    op.body.manageOfferOp().baseBalance = baseBalance;
    op.body.manageOfferOp().isBuy = isBuy;
    op.body.manageOfferOp().offerID = offerID;
    op.body.manageOfferOp().price = price;
    op.body.manageOfferOp().quoteBalance = quoteBalance;
    op.body.manageOfferOp().fee = fee;
    op.body.manageOfferOp().orderBookID = ManageOfferOpFrame::SECONDARY_MARKET_ORDER_BOOK_ID;

    return txFromOperation(source, op);
}

ManageOfferResult ManageOfferTestHelper::applyManageOffer(Account& source,
    ManageOfferOp& manageOfferOp, ManageOfferResultCode expectedResult)
{
    auto txFrame = createManageOfferTx(source, manageOfferOp);
    std::vector<LedgerDelta::KeyEntryMap> stateBeforeOps;
    mTestManager->applyCheck(txFrame, stateBeforeOps);
    auto txResult = txFrame->getResult();
    const auto manageOfferResult = txResult.result.results()[0].tr().manageOfferResult();
    auto actualResultCode = manageOfferResult.code();
    REQUIRE(actualResultCode == expectedResult);


    if (actualResultCode != ManageOfferResultCode::SUCCESS)
    {
        return manageOfferResult;
    }

    REQUIRE(stateBeforeOps.size() == 1);
    auto stateBeforeOp = stateBeforeOps[0];
    const auto isCreate = manageOfferOp.offerID == 0;
    if (isCreate)
    {
        ensureCreateSuccess(source, manageOfferOp, manageOfferResult.success(), stateBeforeOp);
        return manageOfferResult;
    }

    ensureDeleteSuccess(source, manageOfferOp, manageOfferResult.success(), stateBeforeOp);
    return manageOfferResult;
}

TransactionFramePtr ManageOfferTestHelper::createManageOfferTx(Account& source,
    ManageOfferOp& manageOfferOp)
{
    Operation op;
    op.body.type(OperationType::MANAGE_OFFER);
    op.body.manageOfferOp() = manageOfferOp;
    return txFromOperation(source, op);
}
}
}
