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

            ManageKeyValueTestHelper* setKey(string256 key);

            ManageKeyValueTestHelper* setResult(ManageKeyValueResultCode resultCode);

            void doAply(Application &app, LedgerDelta &delta, LedgerManager &ledgerManager,
                                    ManageKVAction action, bool require = true);

        private:

            string256 key;
            ManageKeyValueResultCode expectedResult = ManageKeyValueResultCode ::SUCCESS;
        };

        class ManageKeyValueTestBuilder :public OperationBuilder<ManageKeyValueTestBuilder>
        {
        public:

            ManageKeyValueTestBuilder(string256 key, TestManager::pointer &testManager, ManageKVAction action);

            ManageKeyValueTestBuilder copy() override
            {
                return *this;
            }

            Operation buildOp() override ;

            string256 key;
            ManageKVAction kvAction;
            ManageKeyValueOpFrame* kvManager;
            Operation op;
            TransactionFrame* tx;
            OperationResult res;
        };
    }
}


#endif //STELLAR_MANAGEKEYVALUETESTHELPER_H
