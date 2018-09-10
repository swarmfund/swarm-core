
#include "SetIdentityPolicyTestHelper.h"
#include <lib/catch.hpp>
#include "ledger/AccountRolePolicyHelper.h"
#include "transactions/SetIdentityPolicyOpFrame.h"

namespace stellar
{
namespace txtest
{

SetIdentityPolicyTestHelper::SetIdentityPolicyTestHelper(TestManager::pointer testManager) : TxHelper(testManager) {

}

TransactionFramePtr SetIdentityPolicyTestHelper::createSetIdentityPolicyTx(Account &source,
                                                                           IdentityPolicyEntry policyEntry,
                                                                           bool isDelete) {
    Operation op;
    op.body.type(OperationType::SET_IDENTITY_POLICY);

    SetIdentityPolicyOp &setIdentityPolicyOp = op.body.setIdentityPolicyOp();

    setIdentityPolicyOp.id = policyEntry.id;
    if (!isDelete) {
        setIdentityPolicyOp.data.activate();
        setIdentityPolicyOp.data->resource = policyEntry.resource;
        setIdentityPolicyOp.data->priority = policyEntry.priority;
        setIdentityPolicyOp.data->action = policyEntry.action;
        setIdentityPolicyOp.data->effect = policyEntry.effect;
    }
    setIdentityPolicyOp.ext.v(LedgerVersion::EMPTY_VERSION);

    return TxHelper::txFromOperation(source, op, nullptr);
}

void SetIdentityPolicyTestHelper::applySetIdentityPolicyTx(Account &source,
                                                           IdentityPolicyEntry policyEntry,
                                                           bool isDelete,
                                                           SetIdentityPolicyResultCode expectedResult)
{
    TransactionFramePtr txFrame;
    txFrame = createSetIdentityPolicyTx(source, policyEntry, isDelete);
    mTestManager->applyCheck(txFrame);

    auto txResult = txFrame->getResult();
    auto actualResult = SetIdentityPolicyOpFrame::getInnerCode(txResult.result.results()[0]);

    REQUIRE(actualResult == expectedResult);

    if (actualResult != SetIdentityPolicyResultCode::SUCCESS) {
        return;
    }

    SetIdentityPolicyResult setIdentityPolicyResult = txResult.result.results()[0].tr().setIdentityPolicyResult();

    auto storedIdentityPolicy =
            AccountRolePolicyHelper::Instance()->loadPolicy(setIdentityPolicyResult.success().identityPolicyID,
                                                            policyEntry.ownerID, mTestManager->getDB());
    if (isDelete)
    {
        REQUIRE(!storedIdentityPolicy);
    }
    else
    {
        // update auto generated id of identity policy
        policyEntry.id = storedIdentityPolicy->getIdentityPolicy().id;

        REQUIRE(storedIdentityPolicy->getIdentityPolicy() == policyEntry);
    }
}

IdentityPolicyEntry SetIdentityPolicyTestHelper::createIdentityPolicyEntry(uint64_t id,
                                                                           AccountID owner,
                                                                           SetIdentityPolicyData *data)
{
    LedgerEntry le;
    le.data.type(LedgerEntryType::IDENTITY_POLICY);
    auto policyEntry = le.data.identityPolicy();

    policyEntry.id = id;
    policyEntry.ownerID = owner;
    if (data != nullptr) {
        policyEntry.priority = data->priority;
        policyEntry.resource = data->resource;
        policyEntry.action = data->action;
        policyEntry.effect = data->effect;
    }

    return policyEntry;
}

}
}

