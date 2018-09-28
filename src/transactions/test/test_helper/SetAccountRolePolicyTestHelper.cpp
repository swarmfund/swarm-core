#include "SetAccountRolePolicyTestHelper.h"
#include "ledger/AccountRolePolicyHelper.h"
#include "ledger/StorageHelperImpl.h"
#include "transactions/ManageAccountRolePolicyOpFrame.h"
#include <lib/catch.hpp>

namespace stellar
{
namespace txtest
{

using xdr::operator==;

SetAccountRolePolicyTestHelper::SetAccountRolePolicyTestHelper(
    TestManager::pointer testManager)
    : TxHelper(testManager)
{
}

TransactionFramePtr
SetAccountRolePolicyTestHelper::createSetAccountRolePolicyTx(
    Account& source, AccountRolePolicyEntry policyEntry,
    ManageAccountRolePolicyOpAction action)
{
    Operation op;
    op.body.type(OperationType::MANAGE_ACCOUNT_ROLE_POLICY);
    ManageAccountRolePolicyOp& manageAccountRolePolicyOp =
        op.body.manageAccountRolePolicyOp();
    manageAccountRolePolicyOp.data.action(action);

    switch (action)
    {
    case ManageAccountRolePolicyOpAction::CREATE:
        manageAccountRolePolicyOp.data.createData().roleID =
            policyEntry.accountRoleID;
        manageAccountRolePolicyOp.data.createData().resource =
            policyEntry.resource;
        manageAccountRolePolicyOp.data.createData().action = policyEntry.action;
        manageAccountRolePolicyOp.data.createData().effect = policyEntry.effect;
        break;
    case ManageAccountRolePolicyOpAction::UPDATE:
        manageAccountRolePolicyOp.data.updateData().policyID =
            policyEntry.accountRolePolicyID;
        manageAccountRolePolicyOp.data.updateData().roleID =
            policyEntry.accountRoleID;
        manageAccountRolePolicyOp.data.updateData().resource =
            policyEntry.resource;
        manageAccountRolePolicyOp.data.updateData().action = policyEntry.action;
        manageAccountRolePolicyOp.data.updateData().effect = policyEntry.effect;
        break;
    case ManageAccountRolePolicyOpAction::REMOVE:
        manageAccountRolePolicyOp.data.removeData().policyID =
            policyEntry.accountRolePolicyID;
        break;
    default:
        throw std::runtime_error("Unknown action");
    }
    manageAccountRolePolicyOp.ext.v(LedgerVersion::EMPTY_VERSION);

    return TxHelper::txFromOperation(source, op, nullptr);
}

void
SetAccountRolePolicyTestHelper::applySetIdentityPolicyTx(
    Account& source, AccountRolePolicyEntry& policyEntry, ManageAccountRolePolicyOpAction action,
    ManageAccountRolePolicyResultCode expectedResult)
{
    TransactionFramePtr txFrame;
    txFrame = createSetAccountRolePolicyTx(source, policyEntry, action);
    mTestManager->applyCheck(txFrame);

    auto txResult = txFrame->getResult();
    auto actualResult = ManageAccountRolePolicyOpFrame::getInnerCode(
        txResult.result.results()[0]);

    REQUIRE(actualResult == expectedResult);

    if (actualResult != ManageAccountRolePolicyResultCode::SUCCESS)
    {
        return;
    }

    ManageAccountRolePolicyResult result =
        txResult.result.results()[0].tr().manageAccountRolePolicyResult();

    StorageHelperImpl storageHelperImpl(mTestManager->getDB(), nullptr);
    AccountRolePolicyHelper rolePolicyHelper(storageHelperImpl);
    LedgerKey affectedPolicyKey;
    affectedPolicyKey.type(LedgerEntryType::ACCOUNT_ROLE_POLICY);
    affectedPolicyKey.accountRolePolicy().accountRolePolicyID =
        result.success().accountRolePolicyID;
    affectedPolicyKey.accountRolePolicy().ownerID = source.key.getPublicKey();

    EntryFrame::pointer affectedPolicy =
        rolePolicyHelper.storeLoad(affectedPolicyKey);
    if (action == ManageAccountRolePolicyOpAction::REMOVE)
    {
        REQUIRE(!affectedPolicy);
    }
    else
    {
        auto affectedAccountRolePolicy =
            std::dynamic_pointer_cast<AccountRolePolicyFrame>(affectedPolicy);
        REQUIRE(affectedAccountRolePolicy);
        // update auto generated id of identity policy
        policyEntry.accountRolePolicyID = affectedAccountRolePolicy->getID();

        REQUIRE(affectedAccountRolePolicy->getPolicyEntry() == policyEntry);
    }
}

AccountRolePolicyEntry
SetAccountRolePolicyTestHelper::createAccountRolePolicyEntry(
    uint64_t id, AccountID owner, PolicyDetails* details,
    AccountRolePolicyEffect effect)
{
    LedgerEntry le;
    le.data.type(LedgerEntryType::ACCOUNT_ROLE_POLICY);
    auto policyEntry = le.data.accountRolePolicy();

    policyEntry.accountRoleID = id;
    policyEntry.ownerID = owner;
    policyEntry.effect = effect;
    if (details != nullptr)
    {
        policyEntry.resource = details->getResourceID();
        policyEntry.action = details->getAction();
    }

    return policyEntry;
}

} // namespace txtest
} // namespace stellar
