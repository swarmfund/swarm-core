#include "SetAccountRolePolicyTestHelper.h"
#include "ledger/AccountRolePolicyHelper.h"
#include "transactions/SetAccountRolePolicyOpFrame.h"
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
    Account& source, AccountRolePolicyEntry policyEntry, bool isDelete)
{
    Operation op;
    op.body.type(OperationType::SET_ACCOUNT_ROLE_POLICY);

    SetAccountRolePolicyOp& setAccountRolePolicyOp =
        op.body.setAccountRolePolicyOp();

    setAccountRolePolicyOp.id = policyEntry.accountRolePolicyID;
    if (!isDelete)
    {
        setAccountRolePolicyOp.data.activate();
        setAccountRolePolicyOp.data->resource = policyEntry.resource;
        setAccountRolePolicyOp.data->action = policyEntry.action;
        setAccountRolePolicyOp.data->effect = policyEntry.effect;
        setAccountRolePolicyOp.data->roleID = policyEntry.accountRoleID;
    }
    setAccountRolePolicyOp.ext.v(LedgerVersion::EMPTY_VERSION);

    return TxHelper::txFromOperation(source, op, nullptr);
}

void
SetAccountRolePolicyTestHelper::applySetIdentityPolicyTx(
    Account& source, AccountRolePolicyEntry policyEntry, bool isDelete,
    SetAccountRolePolicyResultCode expectedResult)
{
    TransactionFramePtr txFrame;
    txFrame = createSetAccountRolePolicyTx(source, policyEntry, isDelete);
    mTestManager->applyCheck(txFrame);

    auto txResult = txFrame->getResult();
    auto actualResult =
        SetAccountRolePolicyOpFrame::getInnerCode(txResult.result.results()[0]);

    REQUIRE(actualResult == expectedResult);

    if (actualResult != SetAccountRolePolicyResultCode::SUCCESS)
    {
        return;
    }

    SetAccountRolePolicyResult result =
        txResult.result.results()[0].tr().setAccountRolePolicyResult();

    AccountRolePolicyHelper rolePolicyHelper(mTestManager->getDB());
    LedgerKey affectedPolicyKey;
    affectedPolicyKey.type(LedgerEntryType::ACCOUNT_ROLE_POLICY);
    affectedPolicyKey.accountRolePolicy().accountRolePolicyID =
        result.success().accountRolePolicyID;
    affectedPolicyKey.accountRolePolicy().ownerID = source.key.getPublicKey();

    EntryFrame::pointer affectedPolicy =
        rolePolicyHelper.storeLoad(affectedPolicyKey);
    if (isDelete)
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
