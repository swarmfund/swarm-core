#ifndef STELLAR_MANAGEKEYVALUETESTHELPER_H
#define STELLAR_MANAGEKEYVALUETESTHELPER_H


#include <transactions/ManageKeyValueOpFrame.h>
#include "TxHelper.h"



namespace stellar{
    namespace txtest{

        class ManageKeyValueTestHelper : public TxHelper
        {
        public:

            explicit ManageKeyValueTestHelper(TestManager::pointer testManager);

            ManageKeyValueTestHelper copy();

            ManageKeyValueTestHelper* setKey(longstring key);

            ManageKeyValueTestHelper* setValue(uint32 value);

            ManageKeyValueTestHelper* setResult(ManageKeyValueResultCode resultCode);

            void doApply(Application &app, ManageKVAction action, bool require, KeyValueEntryType type =
                                                                                KeyValueEntryType::UINT32);

        private:

            uint32 value;
            longstring key;
            ManageKeyValueResultCode expectedResult = ManageKeyValueResultCode ::SUCCESS;
        };

        class ManageKeyValueTestBuilder :public OperationBuilder<ManageKeyValueTestBuilder>
        {
        public:

            ManageKeyValueTestBuilder(string256 key, TestManager::pointer &testManager,
                                                    ManageKVAction action, uint32 value,
                                                    KeyValueEntryType type = KeyValueEntryType::UINT32);

            ManageKeyValueTestBuilder copy() override
            {
                return *this;
            }

            Operation buildOp() override;

            longstring key;
            uint32 value;
            KeyValueEntryType type;
            ManageKVAction kvAction;
            ManageKeyValueOpFrame* kvManager;
            Operation op;
            TransactionFramePtr tx;
            OperationResult res;
        };
    }
}


#endif //STELLAR_MANAGEKEYVALUETESTHELPER_H
