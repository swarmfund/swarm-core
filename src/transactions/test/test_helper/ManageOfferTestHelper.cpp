#include <ledger/SaleAnteHelper.h>
#include "ManageOfferTestHelper.h"
#include "ledger/AssetPairHelper.h"
#include "ledger/OfferHelper.h"
#include "ledger/BalanceHelperLegacy.h"
#include "transactions/dex/OfferManager.h"
#include "test/test_marshaler.h"

namespace stellar
{
namespace txtest
{

void ManageOfferTestHelper::ensureDeleteSuccess(Account& source, ManageOfferOp op,
                                                const ManageOfferSuccessResult success, LedgerDelta::KeyEntryMap& stateBeforeTx)
{
    LedgerKey offerKey;
    offerKey.type(LedgerEntryType::OFFER_ENTRY);
    offerKey.offer().ownerID = source.key.getPublicKey();
    offerKey.offer().offerID = op.offerID;

    auto offer = std::make_shared<OfferFrame>(stateBeforeTx[offerKey]->mEntry);

    // ensure offer was removed
    auto removedOfferFrame = OfferHelper::Instance()->loadOffer(offer->getOffer().ownerID, offer->getOfferID(), mTestManager->getDB());
    REQUIRE(!removedOfferFrame);

    // check if balance unlocked
    LedgerKey balanceKey;
    balanceKey.type(LedgerEntryType::BALANCE);
    balanceKey.balance().balanceID = offer->getLockedBalance();

    auto sellingBalanceBefore = stateBeforeTx[balanceKey]->mEntry.data.balance();
    auto sellingBalanceAfter = BalanceHelperLegacy::Instance()->mustLoadBalance(offer->getLockedBalance(), mTestManager->getDB());

    REQUIRE(sellingBalanceAfter->getLocked() == sellingBalanceBefore.locked - offer->getLockedAmount());
}

void ManageOfferTestHelper::ensureCreateSuccess(Account& source, ManageOfferOp op,
    ManageOfferSuccessResult success, LedgerDelta::KeyEntryMap& stateBeforeTx)
{
    auto &db = mTestManager->getDB();

    auto& offerResult = success.offer;
    auto claimedOffers = success.offersClaimed;

    switch (offerResult.effect())
    {
    case ManageOfferEffect::CREATED:
        {
            REQUIRE(op.offerID == 0);
            auto offer = OfferHelper::Instance()->loadOffer(source.key.getPublicKey(), offerResult.offer().offerID, db);
            REQUIRE(!!offer);
            auto &offerEntry = offer->getOffer();
            REQUIRE(offerEntry == offerResult.offer());
            REQUIRE(offerEntry.price == op.price);
            REQUIRE(offerEntry.baseBalance == op.baseBalance);
            REQUIRE(offerEntry.quoteBalance == op.quoteBalance);
            //check balance was locked
            LedgerKey balanceKey;
            balanceKey.type(LedgerEntryType::BALANCE);
            balanceKey.balance().balanceID = offer->getLockedBalance();
            auto balanceBefore = stateBeforeTx[balanceKey]->mEntry.data.balance();
            auto balanceAfter = BalanceHelperLegacy::Instance()->mustLoadBalance(offer->getLockedBalance(), db);

            auto saleAnteAfter = SaleAnteHelper::Instance()->loadSaleAnte(offerEntry.orderBookID,
                                                                          balanceAfter->getBalanceID(), db);
            if (!!saleAnteAfter) {
                REQUIRE(balanceAfter->getLocked() == balanceBefore.locked + offer->getLockedAmount() +
                                                     saleAnteAfter->getAmount());
            } else {
                REQUIRE(balanceAfter->getLocked() == balanceBefore.locked + offer->getLockedAmount());
            }

            break;
        }
    case ManageOfferEffect::UPDATED:
        throw std::runtime_error("Update offer is not supported");
    case ManageOfferEffect::DELETED:
        {
        if (op.offerID == 0)
            break;
        auto offer = OfferHelper::Instance()->loadOffer(source.key.getPublicKey(), op.offerID, mTestManager->getDB());
        REQUIRE(!offer);
        break;
        }
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
        ManageOfferTestHelper::ensureCreateSuccess(source, manageOfferOp, manageOfferResult.success(), stateBeforeOp);
        return manageOfferResult;
    }

    ManageOfferTestHelper::ensureDeleteSuccess(source, manageOfferOp, manageOfferResult.success(), stateBeforeOp);
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
