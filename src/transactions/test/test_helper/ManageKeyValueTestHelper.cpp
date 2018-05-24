#include <transactions/test/TxTests.h>
#include "ManageKeyValueTestHelper.h"
#include "ledger/KeyValueHelper.h"
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
            this->ui32Value = value;
            return this;
        }

        txtest::ManageKeyValueTestHelper *ManageKeyValueTestHelper::setValue(std::string value) {
            this->strValue = value;
            return this;
        }

        ManageKeyValueTestHelper* ManageKeyValueTestHelper::setResult(ManageKeyValueResultCode resultCode)
        {
            this->expectedResult = resultCode;
            return  this;
        }

        void ManageKeyValueTestHelper::doApply(Application &app, ManageKVAction action, bool require,
                                               KeyValueEntryType type)
        {
            LedgerDelta delta(mTestManager->getLedgerManager().getCurrentLedgerHeader(), mTestManager->getDB());

            ManageKeyValueTestBuilder builder(key, mTestManager, action, ui32Value,  strValue, type);

            bool isApplied = builder.kvManager->doApply(app, delta, mTestManager->getLedgerManager());
            bool isValid = builder.kvManager->doCheckValid(app);

            REQUIRE((isApplied && isValid) == require);
            REQUIRE(builder.kvManager->getInnerCode(builder.kvManager->getResult()) == expectedResult);

            auto actualKeyValue = KeyValueHelper::Instance()->loadKeyValue(key, app.getDatabase());
            switch (action) {
                case ManageKVAction ::PUT:
                {
                    REQUIRE(!!actualKeyValue);

                    switch (actualKeyValue->getKeyValue().value.type()){
                        case KeyValueEntryType::UINT32: {
                            REQUIRE(actualKeyValue->getKeyValue().value.ui32Value() == ui32Value);
                            break;
                        }
                        case KeyValueEntryType::STRING: {
                            REQUIRE(actualKeyValue->getKeyValue().value.stringValue() == strValue);
                            break;
                        }
                    }
                    break;
                }
                case ManageKVAction ::DELETE:
                {
                    REQUIRE(!actualKeyValue);
                    break;
                }
            }
        }


        Operation ManageKeyValueTestBuilder::buildOp()
        {
            Operation op;
            op.body.type(OperationType::MANAGE_KEY_VALUE);
            op.body.manageKeyValueOp() = ManageKeyValueOp();
            op.body.manageKeyValueOp().key = key;
            op.body.manageKeyValueOp().action.action(kvAction);

            if(kvAction == ManageKVAction::PUT) {
                op.body.manageKeyValueOp().action.value().value.type(this->type);
                switch (this->type){
                    case KeyValueEntryType::UINT32: {
                        op.body.manageKeyValueOp().action.value().value.ui32Value() = this->ui32Value;
                        break;
                    }
                    case KeyValueEntryType::STRING: {
                        op.body.manageKeyValueOp().action.value().value.stringValue() = this->strValue;
                        break;
                    }
                }
                op.body.manageKeyValueOp().action.value().key = key;
            }
            return op;
        }

        ManageKeyValueTestBuilder::ManageKeyValueTestBuilder(string256 key, TestManager::pointer &testManager,
                                                             ManageKVAction action, uint32 ui32Value,
                                                             std::string strValue, KeyValueEntryType type)
                :key(key),
                 kvAction(action)
        {
            this->ui32Value = ui32Value;
            this->strValue = strValue;
            this->type = type;
            tx = this->buildTx(testManager);
            op = buildOp();
            res = OperationResult(OperationResultCode::opINNER);
            res.tr().type(OperationType::MANAGE_KEY_VALUE);
            kvManager = new ManageKeyValueOpFrame(op,res,*tx);
        }

    }
}