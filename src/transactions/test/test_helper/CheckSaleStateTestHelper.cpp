#include "CheckSaleStateTestHelper.h"
#include "transactions/test/TxTests.h"
#include "ledger/OfferHelper.h"
#include "ledger/SaleHelper.h"
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

void CheckSaleStateHelper::ensureCancel(const CheckSaleStateSuccess result,
    StateBeforeTxHelper& stateBeforeTx) const
{
    // asset unlocked
    const auto sale = stateBeforeTx.getSale(result.saleID);
    auto baseAssetBeforeTx = stateBeforeTx.getAssetEntry(sale->getBaseAsset());
    auto baseAssetAfterTx = AssetHelper::Instance()->loadAsset(sale->getBaseAsset(), mTestManager->getDB());
    auto currentCupBaseAsset = sale->getBaseAmountForCurrentCap();
    REQUIRE(baseAssetBeforeTx.pendingIssuance == baseAssetAfterTx->getPendingIssuance() + currentCupBaseAsset);
    REQUIRE(baseAssetBeforeTx.availableForIssueance + currentCupBaseAsset == baseAssetAfterTx->getAvailableForIssuance());
    // balances unlocked
}

void CheckSaleStateHelper::ensureClose(const CheckSaleStateSuccess result,
    StateBeforeTxHelper& stateBeforeTx) const
{
    const auto sale = stateBeforeTx.getSale(result.saleID);
    auto baseAssetBeforeTx = stateBeforeTx.getAssetEntry(sale->getBaseAsset());
    auto baseAssetAfterTx = AssetHelper::Instance()->loadAsset(sale->getBaseAsset(), mTestManager->getDB());
    auto currentCupBaseAsset = sale->getBaseAmountForCurrentCap();
    REQUIRE(baseAssetBeforeTx.pendingIssuance == baseAssetAfterTx->getPendingIssuance() + currentCupBaseAsset);
    REQUIRE(baseAssetBeforeTx.availableForIssueance == baseAssetAfterTx->getAvailableForIssuance());
    REQUIRE(baseAssetAfterTx->getIssued() == baseAssetBeforeTx.issued + currentCupBaseAsset);
    REQUIRE(baseAssetAfterTx->getMaxIssuanceAmount() == baseAssetBeforeTx.issued + currentCupBaseAsset);
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
