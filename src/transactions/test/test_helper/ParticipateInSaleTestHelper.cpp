#include "ParticipateInSaleTestHelper.h"
#include "transactions/test/TxTests.h"
#include "ledger/OfferHelper.h"
#include "xdrpp/printer.h"
#include "ledger/SaleHelper.h"

namespace stellar
{
namespace txtest
{
ParticipateInSaleTestHelper::ParticipateInSaleTestHelper(
    const TestManager::pointer testManager): ManageOfferTestHelper(testManager)
{
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
    auto saleBeforeTx = stateBeforeTx[saleAfterTx->getKey()]->mEntry.data.sale();
    REQUIRE(saleBeforeTx.currentCap == saleAfterTx->getCurrentCap() + offerBeforeTx.quoteAmount);
    ManageOfferTestHelper::ensureDeleteSuccess(source, op, success, stateBeforeTx);
}

void ParticipateInSaleTestHelper::ensureCreateSuccess(Account& source,
                                                      const ManageOfferOp op, ManageOfferSuccessResult success,
    LedgerDelta::KeyEntryMap& stateBeforeTx)
{
    auto saleAfterTx = SaleHelper::Instance()->loadSale(op.orderBookID, mTestManager->getDB());
    auto saleBeforeTx = stateBeforeTx[saleAfterTx->getKey()]->mEntry.data.sale();
    REQUIRE(saleBeforeTx.currentCap + success.offer.offer().quoteAmount == saleAfterTx->getCurrentCap());
    return ManageOfferTestHelper::ensureCreateSuccess(source, op, success, stateBeforeTx);
}
}
}
