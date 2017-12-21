#ifndef STELLAR_CREATEACCOUNTTESTHELPER_H
#define STELLAR_CREATEACCOUNTTESTHELPER_H

#include "TxHelper.h"

namespace stellar {
    namespace txtest {

        class CreateAccountTestHelper;

        class CreateAccountChecker {
        public:
            TestManager::pointer mTestManager;

            explicit CreateAccountChecker(TestManager::pointer testManager);

            void doCheck(CreateAccountTestHelper *testHelper,
                         CreateAccountResultCode actualResultCode);
        };

        class CreateAccountTestHelper : TxHelper {
        public:
            friend CreateAccountChecker;

            explicit CreateAccountTestHelper(TestManager::pointer testManager);

            CreateAccountResultCode applyTx();

            [[deprecated]]
            CreateAccountResultCode applyCreateAccountTx(Account &from, PublicKey to, AccountType accountType,
                                                         Account* signer = nullptr, AccountID *referrer = nullptr,
                                                         int32 policies = -1,
                                                         CreateAccountResultCode expectedResult = CreateAccountResultCode::SUCCESS);


            TransactionFramePtr createCreateAccountTx();

            CreateAccountTestHelper setFromAccount(Account from);

            CreateAccountTestHelper setToPublicKey(PublicKey to);

            CreateAccountTestHelper setType(AccountType accountType);

            CreateAccountTestHelper setType(int32_t accountType);

            CreateAccountTestHelper setSigner(Account *signer);

            CreateAccountTestHelper setReferrer(AccountID *referrer);

            CreateAccountTestHelper setPolicies(int32 policies);

            CreateAccountTestHelper setPolicies(AccountPolicies policies);

            CreateAccountTestHelper setResultCode(CreateAccountResultCode expectedResult);

        private:
            Account from;
            PublicKey to;
            AccountType accountType;
            Account *signer = nullptr;
            AccountID *referrer = nullptr;
            int32 policies = -1;
            CreateAccountResultCode expectedResult = CreateAccountResultCode::SUCCESS;
        };
    }

}


#endif //STELLAR_CREATEACCOUNTTESTHELPER_H
