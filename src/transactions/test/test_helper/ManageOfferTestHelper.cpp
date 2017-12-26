#include "ManageOfferTestHelper.h"
#include "transactions/test/TxTests.h"
#include "ledger/AssetPairHelper.h"
#include "ledger/OfferHelper.h"
#include "ledger/BalanceHelper.h"

namespace stellar
{
namespace txtest
{
ManageOfferTestHelper::ManageOfferTestHelper(const TestManager::pointer testManager) : TxHelper(testManager)
{
}

ManageOfferResult ManageOfferTestHelper::applyManageOffer(Account& source, const uint64_t offerID, BalanceID const& baseBalance,
    BalanceID const& quoteBalance, const int64_t amount, const int64_t price, const bool isBuy,
                                                              const int64_t fee, ManageOfferResultCode expectedResult)
{
    Database& db = mTestManager->getDB();

    const auto expectedOfferID = mTestManager->getLedgerDelta().getHeaderFrame().getLastGeneratedID(LedgerEntryType::OFFER_ENTRY) + 1;
    auto txFrame = creatManageOfferTx(source, offerID, baseBalance, quoteBalance, amount, price ,isBuy, fee);
    mTestManager->applyCheck(txFrame);
    auto txResult = txFrame->getResult();
    const auto manageOfferResult = txResult.result.results()[0].tr().manageOfferResult();
    auto actualResultCode = manageOfferResult.code();
    REQUIRE(actualResultCode == expectedResult);


    if (actualResultCode != ManageOfferResultCode::SUCCESS)
    {
        return manageOfferResult;
    }

    auto& offerResult = manageOfferResult.success().offer;

    auto claimedOffers = manageOfferResult.success().offersClaimed;
    if (!claimedOffers.empty())
    {
        auto currentPrice = claimedOffers[claimedOffers.size() - 1].currentPrice;
        auto assetPair = AssetPairHelper::Instance()->loadAssetPair(manageOfferResult.success().baseAsset, manageOfferResult.success().quoteAsset, db, nullptr);
        REQUIRE(assetPair->getCurrentPrice() == currentPrice);
    }

    auto offer = OfferHelper::Instance()->loadOffer(source.key.getPublicKey(), expectedOfferID, db);

    switch (offerResult.effect())
    {
    case ManageOfferEffect::CREATED:
    case ManageOfferEffect::UPDATED:
    {
        REQUIRE(!!offer);
        auto& offerEntry = offer->getOffer();
        REQUIRE(offerEntry == offerResult.offer());
        REQUIRE(offerEntry.price == price);
        REQUIRE(offerEntry.baseBalance == baseBalance);
        REQUIRE(offerEntry.quoteBalance == quoteBalance);
    }
    break;
    case ManageOfferEffect::DELETED:
        REQUIRE(!offer);
        break;
    default:
        throw std::runtime_error("Unexpected offer effect");
    }
    
    return manageOfferResult;
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

    return txFromOperation(source, op);
}
}
}
