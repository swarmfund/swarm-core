#include <ledger/SaleAnteHelper.h>
#include <ledger/FeeHelper.h>
#include <ledger/AccountHelper.h>
#include <ledger/BalanceHelper.h>
#include "ParticipateInSaleTestHelper.h"
#include "transactions/test/TxTests.h"
#include "ledger/OfferHelper.h"
#include "ledger/SaleHelper.h"
#include "test/test_marshaler.h"

namespace stellar
{
namespace txtest
{
ParticipateInSaleTestHelper::ParticipateInSaleTestHelper(
    const TestManager::pointer testManager): ManageOfferTestHelper(testManager)
{
}

    BalanceEntry getBalance(LedgerDelta::KeyEntryMap& stateBeforeTx, BalanceID const& balanceID)
{
        LedgerKey key;
        key.type(LedgerEntryType::BALANCE);
        key.balance().balanceID = balanceID;
        return stateBeforeTx[key]->mEntry.data.balance();
}

void ParticipateInSaleTestHelper::ensureDeleteSuccess(Account& source,
    ManageOfferOp op, const ManageOfferSuccessResult success,
    LedgerDelta::KeyEntryMap& stateBeforeTx)
{
    LedgerKey key;
    key.type(LedgerEntryType::OFFER_ENTRY);
    key.offer().offerID = op.offerID;
    key.offer().ownerID = source.key.getPublicKey();
    auto offerBeforeTx = stateBeforeTx[key]->mEntry.data.offer();
    auto saleAfterTx = SaleHelper::Instance()->loadSale(op.orderBookID, mTestManager->getDB());
    auto balanceBeforeTx = getBalance(stateBeforeTx, offerBeforeTx.quoteBalance);
    SaleFrame saleBeforeTx(stateBeforeTx[saleAfterTx->getKey()]->mEntry);
    REQUIRE(saleBeforeTx.getSaleQuoteAsset(balanceBeforeTx.asset).currentCap == saleAfterTx->getSaleQuoteAsset(balanceBeforeTx.asset).currentCap + offerBeforeTx.quoteAmount);
    REQUIRE(saleBeforeTx.getSaleEntry().maxAmountToBeSold == saleAfterTx->getSaleEntry().maxAmountToBeSold);
    REQUIRE(saleBeforeTx.getSaleEntry().currentCapInBase == saleAfterTx->getSaleEntry().currentCapInBase + offerBeforeTx.baseAmount);
    ManageOfferTestHelper::ensureDeleteSuccess(source, op, success, stateBeforeTx);
}

void ParticipateInSaleTestHelper::ensureCreateSuccess(Account& source,
                                                      const ManageOfferOp op, ManageOfferSuccessResult success,
    LedgerDelta::KeyEntryMap& stateBeforeTx)
{
    auto &db = mTestManager->getDB();

    auto saleAfterTx = SaleHelper::Instance()->loadSale(op.orderBookID, db);
    auto sale = stateBeforeTx.find(saleAfterTx->getKey());
    REQUIRE(sale != stateBeforeTx.end());
    SaleFrame saleBeforeTx(sale->second->mEntry);
    auto balanceBeforeTx = getBalance(stateBeforeTx, success.offer.offer().quoteBalance);
    REQUIRE(saleBeforeTx.getSaleQuoteAsset(balanceBeforeTx.asset).currentCap + 
        success.offer.offer().quoteAmount == saleAfterTx->getSaleQuoteAsset(balanceBeforeTx.asset).currentCap);
    REQUIRE(saleBeforeTx.getSaleEntry().maxAmountToBeSold == saleAfterTx->getSaleEntry().maxAmountToBeSold);
    if (saleBeforeTx.getSaleType() != SaleType::CROWD_FUNDING)
    {
        REQUIRE(saleBeforeTx.getSaleEntry().currentCapInBase + op.amount == saleAfterTx->getSaleEntry().currentCapInBase);
    }

    auto sourceAccountFrame = AccountHelper::Instance()->loadAccount(source.key.getPublicKey(), db);
    auto investFee = FeeHelper::Instance()->loadForAccount(FeeType::INVEST_FEE, balanceBeforeTx.asset, 0,
                                                           sourceAccountFrame, op.amount, db);
    if (!!investFee) {
        auto saleAnteAfterTx = SaleAnteHelper::Instance()->loadSaleAnte(saleAfterTx->getID(),
                                                                        balanceBeforeTx.balanceID, db);
        REQUIRE(!!saleAnteAfterTx);

        auto balanceAfterTx = BalanceHelper::Instance()->loadBalance(balanceBeforeTx.balanceID, db);
        REQUIRE(balanceAfterTx->getLocked() - success.offer.offer().quoteAmount ==
                balanceBeforeTx.locked + saleAnteAfterTx->getAmount());
    }

    return ManageOfferTestHelper::ensureCreateSuccess(source, op, success, stateBeforeTx);
}
}
}
