#include <transactions/test/TxTests.h>
#include "ManageKeyValueTestHelper.h"
#include "ledger/KeyValueHelperLegacy.h"
#include "ledger/LedgerDeltaImpl.h"
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

        txtest::ManageKeyValueTestHelper *ManageKeyValueTestHelper::setUi32Value(uint32 value) {
            this->value.type(KeyValueEntryType::UINT32);
            this->value.ui32Value() = value;
            return this;
        }

        txtest::ManageKeyValueTestHelper *ManageKeyValueTestHelper::setValue(std::string value) {
            this->value.type(KeyValueEntryType::STRING);
            this->value.stringValue() = value;
            return this;
        }
        txtest::ManageKeyValueTestHelper *ManageKeyValueTestHelper::setUi64Value(uint64 value) {
            this->value.type(KeyValueEntryType::UINT64);
            this->value.ui64Value() = value;
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
            LedgerDeltaImpl delta(mTestManager->getLedgerManager().getCurrentLedgerHeader(), mTestManager->getDB());

            ManageKeyValueTestBuilder builder(key, mTestManager, action, value, type);

            bool isApplied = builder.kvManager->doApply(app, delta, mTestManager->getLedgerManager());
            bool isValid = builder.kvManager->doCheckValid(app);

            REQUIRE((isApplied && isValid) == require);
            REQUIRE(builder.kvManager->getInnerCode(builder.kvManager->getResult()) == expectedResult);

            auto actualKeyValue = KeyValueHelperLegacy::Instance()->loadKeyValue(key, app.getDatabase());
            switch (action) {
                case ManageKVAction::PUT:
                {
                    REQUIRE(!!actualKeyValue);
                    REQUIRE(actualKeyValue->getKeyValue().value == value);
                    break;
                }
                case ManageKVAction::REMOVE:
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
                op.body.manageKeyValueOp().action.value().value = value;
            }
            return op;
        }

        ManageKeyValueTestBuilder::ManageKeyValueTestBuilder(string256 key, TestManager::pointer &testManager,
                                                             ManageKVAction action, KeyValueEntry::_value_t value,
                                                             KeyValueEntryType type)
                :key(key),
                 kvAction(action)
        {
            this->value = value;
            this->type = type;
            tx = this->buildTx(testManager);
            op = buildOp();
            res = OperationResult(OperationResultCode::opINNER);
            res.tr().type(OperationType::MANAGE_KEY_VALUE);
            kvManager = new ManageKeyValueOpFrame(op,res,*tx);
        }

    }
}