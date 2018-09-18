#include "ManageAMLAlertTestHelper.h"
#include "test/test_marshaler.h"
#include "CheckSaleStateTestHelper.h"
#include "ledger/BalanceHelperLegacy.h"

namespace stellar
{

namespace txtest
{

    ManageAMLAlertTestHelper::ManageAMLAlertTestHelper(TestManager::pointer testManager) : TxHelper(testManager)
    {
    }

TransactionFramePtr ManageAMLAlertTestHelper::createAmlAlertTx(Account& source, const BalanceID balance, const uint64 amount,
    std::string reason, std::string reference)
{
    Operation op;
    op.body.type(OperationType::CREATE_AML_ALERT);
    op.body.createAMLAlertRequestOp().reference = reference;
    auto& request = op.body.createAMLAlertRequestOp().amlAlertRequest;
    request.amount = amount;
    request.balanceID = balance;
    request.reason = reason;
    return txFromOperation(source, op);
}

CreateAMLAlertRequestResult ManageAMLAlertTestHelper::applyCreateAmlAlert(
    Account& source, const BalanceID balance, uint64 amount, const std::string reason, const std::string reference,
    CreateAMLAlertRequestResultCode expectedResultCode)
{
    auto tx = createAmlAlertTx(source, balance, amount, reason, reference);
    std::vector<LedgerDelta::KeyEntryMap> stateBeforeOps;
    mTestManager->applyCheck(tx, stateBeforeOps);
    auto txResult = tx->getResult();
    const auto amlAlertResult = txResult.result.results()[0].tr().createAMLAlertRequestResult();
    auto actualResultCode = amlAlertResult.code();
    REQUIRE(actualResultCode == expectedResultCode);


    if (actualResultCode != CreateAMLAlertRequestResultCode::SUCCESS)
    {
        return amlAlertResult;
    }

    REQUIRE(stateBeforeOps.size() == 1);
    const auto stateBeforeOp = stateBeforeOps[0];
    auto stateHelper = StateBeforeTxHelper(stateBeforeOp);
    auto balanceBeforeTx = stateHelper.getBalance(balance);
    auto balanceAfterTx = BalanceHelperLegacy::Instance()->loadBalance(balance, mTestManager->getDB());
    REQUIRE(balanceBeforeTx->getAmount() == balanceAfterTx->getAmount() + amount);
    REQUIRE(balanceBeforeTx->getLocked() == balanceAfterTx->getLocked() - amount);
    return amlAlertResult;
}
}

}
