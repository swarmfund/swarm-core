#include "ManageAccountRoleTestHelper.h"
#include "test/test_marshaler.h"
#include "transactions/ManageAccountRoleOpFrame.h"

namespace stellar
{
namespace txtest
{

ManageAccountRoleTestHelper::ManageAccountRoleTestHelper(
    TestManager::pointer testManager)
    : TxHelper(testManager)
{
}

ManageAccountRoleOp
ManageAccountRoleTestHelper::createCreationOpInput(const std::string& name)
{
    ManageAccountRoleOp opData;
    opData.data.action(ManageAccountRoleOpAction::CREATE);
    opData.data.createData().name = name;
    return opData;
}

ManageAccountRoleOp
ManageAccountRoleTestHelper::createDeletionOpInput(uint64_t accountRoleID)
{
    ManageAccountRoleOp opData;
    opData.data.action(ManageAccountRoleOpAction::REMOVE);
    opData.data.removeData().accountRoleID = accountRoleID;
    return opData;
}

TransactionFramePtr
ManageAccountRoleTestHelper::createAccountRoleTx(Account& source,
                                                 const ManageAccountRoleOp& op)
{
    Operation baseOp;
    baseOp.body.type(OperationType::MANAGE_ACCOUNT_ROLE);
    baseOp.body.manageAccountRoleOp() = op;
    return txFromOperation(source, baseOp, nullptr);
}

ManageAccountRoleResult
ManageAccountRoleTestHelper::applySetAccountRole(
    Account& source, const ManageAccountRoleOp& op,
    ManageAccountRoleResultCode expectedResultCode)
{
    auto& db = mTestManager->getDB();

    TransactionFramePtr txFrame;
    txFrame = createAccountRoleTx(source, op);
    mTestManager->applyCheck(txFrame);

    auto txResult = txFrame->getResult();
    auto actualResultCode =
        ManageAccountRoleOpFrame::getInnerCode(txResult.result.results()[0]);

    REQUIRE(actualResultCode == expectedResultCode);

    auto txFee = mTestManager->getApp().getLedgerManager().getTxFee();
    REQUIRE(txResult.feeCharged == txFee);

    auto opResult = txResult.result.results()[0].tr().manageAccountRoleResult();

    return opResult;
}
} // namespace txtest
} // namespace stellar