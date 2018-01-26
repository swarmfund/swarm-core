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
    REQUIRE(saleBeforeTx.getSaleEntry().hardCapInBase == saleAfterTx->getSaleEntry().hardCapInBase);
    REQUIRE(saleBeforeTx.getSaleEntry().currentCapInBase == saleAfterTx->getSaleEntry().currentCapInBase + offerBeforeTx.baseAmount);
    ManageOfferTestHelper::ensureDeleteSuccess(source, op, success, stateBeforeTx);
}

void ParticipateInSaleTestHelper::ensureCreateSuccess(Account& source,
                                                      const ManageOfferOp op, ManageOfferSuccessResult success,
    LedgerDelta::KeyEntryMap& stateBeforeTx)
{
    auto saleAfterTx = SaleHelper::Instance()->loadSale(op.orderBookID, mTestManager->getDB());
    auto sale = stateBeforeTx.find(saleAfterTx->getKey());
    REQUIRE(sale != stateBeforeTx.end());
    SaleFrame saleBeforeTx(sale->second->mEntry);
    auto balanceBeforeTx = getBalance(stateBeforeTx, success.offer.offer().quoteBalance);
    REQUIRE(saleBeforeTx.getSaleQuoteAsset(balanceBeforeTx.asset).currentCap + 
        success.offer.offer().quoteAmount == saleAfterTx->getSaleQuoteAsset(balanceBeforeTx.asset).currentCap);
    REQUIRE(saleBeforeTx.getSaleEntry().hardCapInBase == saleAfterTx->getSaleEntry().hardCapInBase);
    REQUIRE(saleBeforeTx.getSaleEntry().currentCapInBase + op.amount == saleAfterTx->getSaleEntry().currentCapInBase);
    return ManageOfferTestHelper::ensureCreateSuccess(source, op, success, stateBeforeTx);
}
}
}
