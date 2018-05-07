#include "ManageKeyValueTestHelper.h"
#include "test/test_marshaler.h"

namespace stellar {
    namespace txtest {
        txtest::ManageKeyValueTestHelper::ManageKeyValueTestHelper(txtest::TestManager::pointer testManager) :
                TxHelper(testManager)
        {
        }

        txtest::ManageKeyValueTestHelper txtest::ManageKeyValueTestHelper::copy()
        {
            return *this;
        }

        txtest::ManageKeyValueTestHelper *ManageKeyValueTestHelper::setKey(longstring key)
        {
            this->key = key;
            return this;
        }

        txtest::ManageKeyValueTestHelper *ManageKeyValueTestHelper::setValue(uint32 value) {
            this->value = value;
            return this;
        }

        ManageKeyValueTestHelper* ManageKeyValueTestHelper::setResult(ManageKeyValueResultCode resultCode)
        {
            this->expectedResult = resultCode;
            return  this;
        }

        void ManageKeyValueTestHelper::doApply(Application &app, ManageKVAction action, bool require)
        {
            LedgerDelta delta(mTestManager->getLedgerManager().getCurrentLedgerHeader(),mTestManager->getDB());

            ManageKeyValueTestBuilder builder(key, mTestManager, action, value);

            REQUIRE(builder.kvManager->doApply(app,delta,mTestManager->getLedgerManager()) == require);
            REQUIRE(builder.kvManager->getInnerCode(builder.kvManager->getResult()) == expectedResult);
        }


        Operation ManageKeyValueTestBuilder::buildOp()
        {
            Operation op;
            op.body.type(OperationType::MANAGE_KEY_VALUE);
            op.body.manageKeyValueOp() = ManageKeyValueOp();
            op.body.manageKeyValueOp().key = key;
            op.body.manageKeyValueOp().action.action(kvAction);

            if(kvAction == ManageKVAction::PUT)
            {
                op.body.manageKeyValueOp().action.value().value.type(KeyValueEntryType::UINT32);
                op.body.manageKeyValueOp().action.value().value.defaultMask() = value;
                op.body.manageKeyValueOp().action.value().key = key;
            }
            return op;
        }

        ManageKeyValueTestBuilder::ManageKeyValueTestBuilder(string256 key, TestManager::pointer &testManager,
                                                                     ManageKVAction action, uint32 value)
                :key(key),
                 kvAction(action),
                 value(value)
        {
            auto txFrame = this->buildTx(testManager);
            tx = txFrame.get();
            op = buildOp();
            res = OperationResult(OperationResultCode::opINNER);
            res.tr().type(OperationType::MANAGE_KEY_VALUE);
            kvManager = new ManageKeyValueOpFrame(op,res,*tx);
        }

    }
}