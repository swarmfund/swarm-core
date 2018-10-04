#include "ManageBalanceTestHelper.h"
#include "ledger/AssetHelperLegacy.h"
#include "ledger/BalanceHelperLegacy.h"
#include "transactions/ManageBalanceOpFrame.h"
#include "test/test_marshaler.h"

namespace stellar
{
namespace txtest
{
ManageBalanceTestHelper::ManageBalanceTestHelper(
    TestManager::pointer testManager) : TxHelper(testManager)
{
}

TransactionFramePtr
ManageBalanceTestHelper::createManageBalanceTx(Account& from,
                                               PublicKey& account,
                                               AssetCode asset,
                                               ManageBalanceAction action,
                                               Account* signer)
{
    Operation op;
    op.body.type(OperationType::MANAGE_BALANCE);
    auto& manageBalance = op.body.manageBalanceOp();
    manageBalance.destination = account;
    manageBalance.asset = asset;
    manageBalance.action = action;

    return TxHelper::txFromOperation(from, op, signer);
}

ManageBalanceResult
ManageBalanceTestHelper::applyManageBalanceTx(Account& from, AccountID& account,
                                              AssetCode asset,
                                              ManageBalanceAction action,
                                              ManageBalanceResultCode
                                              expectedResultCode,
                                              Account* signer)
{
    TransactionFramePtr txFrame;

    auto assetHelper = AssetHelperLegacy::Instance();
    auto balanceHelper = BalanceHelperLegacy::Instance();

    std::vector<BalanceFrame::pointer> balances;
    Database& db = mTestManager->getDB();
    balanceHelper->loadBalances(account, balances, db);

    txFrame = createManageBalanceTx(from, account, asset, action, signer);

    mTestManager->applyCheck(txFrame);

    auto txResult = txFrame->getResult();
    auto actualResultCode =
        ManageBalanceOpFrame::getInnerCode(txResult.result.results()[0]);

    REQUIRE(actualResultCode == expectedResultCode);

    auto txFee = mTestManager->getApp().getLedgerManager().getTxFee();
    REQUIRE(txResult.feeCharged == txFee);

    std::vector<BalanceFrame::pointer> balancesAfter;
    balanceHelper->loadBalances(account, balancesAfter, db);

    auto opResult = txResult.result.results()[0].tr().manageBalanceResult();

    if (actualResultCode != ManageBalanceResultCode::SUCCESS)
    {
        REQUIRE(balances.size() == balancesAfter.size());
    }
    else
    {
        switch (action)
        {
        case ManageBalanceAction::CREATE:
        case ManageBalanceAction::CREATE_UNIQUE:
        {
            auto assetFrame = assetHelper->loadAsset(asset, db);
            REQUIRE(balances.size() == balancesAfter.size() - 1);
            auto balance = balanceHelper->
                loadBalance(opResult.success().balanceID, db);
            REQUIRE(balance);
            REQUIRE(balance->getBalance().accountID == account);
            REQUIRE(balance->getBalance().amount == 0);
            REQUIRE(balance->getBalance().asset == asset);
            break;
        }
        case ManageBalanceAction::DELETE_BALANCE:
            REQUIRE(balances.size() == balancesAfter.size() + 1);
            REQUIRE(!balanceHelper->loadBalance(opResult.success().balanceID, db
            ));
        default:
            throw std::runtime_error("Unexpected manage balance action");
        }
    }

    return opResult;
}

void ManageBalanceTestHelper::createBalance(Account& from, PublicKey& account,
                                            AssetCode asset)
{
    applyManageBalanceTx(from, account, asset);
}
}
}
