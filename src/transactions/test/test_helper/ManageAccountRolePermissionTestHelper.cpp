#include "ManageAccountRolePermissionTestHelper.h"
#include "ledger/AccountRolePermissionHelperImpl.h"
#include "ledger/StorageHelperImpl.h"
#include "transactions/ManageAccountRolePermissionOpFrame.h"
#include <lib/catch.hpp>

namespace stellar
{
namespace txtest
{

using xdr::operator==;

ManageAccountRolePermissionTestHelper::ManageAccountRolePermissionTestHelper(
    TestManager::pointer testManager)
    : TxHelper(testManager)
{
}

TransactionFramePtr
ManageAccountRolePermissionTestHelper::createSetAccountRolePermissionTx(
    Account& source, AccountRolePermissionEntry permissionEntry,
    ManageAccountRolePermissionOpAction action)
{
    Operation op;
    op.body.type(OperationType::MANAGE_ACCOUNT_ROLE_PERMISSION);
    ManageAccountRolePermissionOp& manageAccountRolePermissionOp =
        op.body.manageAccountRolePermissionOp();
    manageAccountRolePermissionOp.data.action(action);

    switch (action)
    {
    case ManageAccountRolePermissionOpAction::CREATE:
        manageAccountRolePermissionOp.data.createData().roleID =
            permissionEntry.accountRoleID;
        manageAccountRolePermissionOp.data.createData().opType =
            permissionEntry.opType;
        break;
    case ManageAccountRolePermissionOpAction::UPDATE:
        manageAccountRolePermissionOp.data.updateData().permissionID =
            permissionEntry.permissionID;
        manageAccountRolePermissionOp.data.updateData().roleID =
            permissionEntry.accountRoleID;
        manageAccountRolePermissionOp.data.updateData().opType =
            permissionEntry.opType;
        break;
    case ManageAccountRolePermissionOpAction::REMOVE:
        manageAccountRolePermissionOp.data.removeData().permissionID =
            permissionEntry.permissionID;
        break;
    default:
        throw std::runtime_error("Unknown action");
    }
    manageAccountRolePermissionOp.ext.v(LedgerVersion::EMPTY_VERSION);

    return TxHelper::txFromOperation(source, op, nullptr);
}

void
ManageAccountRolePermissionTestHelper::applySetIdentityPermissionTx(
    Account& source, AccountRolePermissionEntry& permissionEntry,
    ManageAccountRolePermissionOpAction action,
    ManageAccountRolePermissionResultCode expectedResult)
{
    TransactionFramePtr txFrame =
        createSetAccountRolePermissionTx(source, permissionEntry, action);
    mTestManager->applyCheck(txFrame);

    auto txResult = txFrame->getResult();
    auto actualResult = ManageAccountRolePermissionOpFrame::getInnerCode(
        txResult.result.results()[0]);

    REQUIRE(actualResult == expectedResult);

    if (actualResult != ManageAccountRolePermissionResultCode::SUCCESS)
    {
        return;
    }

    ManageAccountRolePermissionResult result =
        txResult.result.results()[0].tr().manageAccountRolePermissionResult();

    StorageHelperImpl storageHelperImpl(mTestManager->getDB(), nullptr);
    AccountRolePermissionHelperImpl rolePermissionHelper(storageHelperImpl);
    LedgerKey affectedPermissionKey;
    affectedPermissionKey.type(LedgerEntryType::ACCOUNT_ROLE_PERMISSION);
    affectedPermissionKey.accountRolePermission().permissionID =
        result.success().permissionID;

    EntryFrame::pointer affectedPermission =
        static_cast<AccountRolePermissionHelper&>(rolePermissionHelper)
            .storeLoad(affectedPermissionKey);
    if (action == ManageAccountRolePermissionOpAction::REMOVE)
    {
        REQUIRE(!affectedPermission);
    }
    else
    {
        auto affectedAccountRolePermission =
            std::dynamic_pointer_cast<AccountRolePermissionFrame>(
                affectedPermission);
        REQUIRE(affectedAccountRolePermission);
        // update auto generated id of identity permission
        permissionEntry.permissionID = affectedAccountRolePermission->getID();

        REQUIRE(affectedAccountRolePermission->getPermissionEntry() ==
                permissionEntry);
    }
}

AccountRolePermissionEntry
ManageAccountRolePermissionTestHelper::createAccountRolePermissionEntry(
    uint64_t id, OperationType opType)
{
    LedgerEntry le;
    le.data.type(LedgerEntryType::ACCOUNT_ROLE_PERMISSION);
    auto permissionEntry = le.data.accountRolePermission();

    permissionEntry.accountRoleID = id;
    permissionEntry.opType = opType;

    return permissionEntry;
}

} // namespace txtest
} // namespace stellar
