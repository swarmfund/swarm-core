#include "SetAccountRoleTestHelper.h"
#include "test/test_marshaler.h"
#include "transactions/SetAccountRoleOpFrame.h"

namespace stellar
{
namespace txtest
{

SetAccountRoleTestHelper::SetAccountRoleTestHelper(
    TestManager::pointer testManager)
    : TxHelper(testManager)
{
}

SetAccountRoleOp
SetAccountRoleTestHelper::createCreationOpInput(const std::string& name)
{
    SetAccountRoleOp opData;
    opData.data.activate();
    opData.data->name = name;
    return opData;
}

SetAccountRoleOp
SetAccountRoleTestHelper::createDeletionOpInput(uint64_t accountRoleID)
{
    SetAccountRoleOp opData;
    opData.id = accountRoleID;
    return opData;
}

TransactionFramePtr
SetAccountRoleTestHelper::createAccountRoleTx(Account& source,
                                              const SetAccountRoleOp& op)
{
    Operation baseOp;
    baseOp.body.type(OperationType::SET_ACCOUNT_ROLE);
    baseOp.body.setAccountRoleOp() = op;
    return txFromOperation(source, baseOp, nullptr);
}

SetAccountRoleResult
SetAccountRoleTestHelper::applySetAccountRole(
    Account& source, const SetAccountRoleOp& op,
    SetAccountRoleResultCode expectedResultCode)
{
    auto& db = mTestManager->getDB();

    TransactionFramePtr txFrame;
    txFrame = createAccountRoleTx(source, op);
    mTestManager->applyCheck(txFrame);

    auto txResult = txFrame->getResult();
    auto actualResultCode =
        SetAccountRoleOpFrame::getInnerCode(txResult.result.results()[0]);

    REQUIRE(actualResultCode == expectedResultCode);

    auto txFee = mTestManager->getApp().getLedgerManager().getTxFee();
    REQUIRE(txResult.feeCharged == txFee);

    auto opResult = txResult.result.results()[0].tr().setAccountRoleResult();

    return opResult;
}
} // namespace txtest
} // namespace stellar