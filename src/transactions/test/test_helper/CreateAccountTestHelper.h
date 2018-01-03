#ifndef STELLAR_CREATEACCOUNTTESTHELPER_H
#define STELLAR_CREATEACCOUNTTESTHELPER_H

#include "TxHelper.h"

namespace stellar {
    namespace txtest {


        class CreateAccountTestBuilder : public OperationBuilder<CreateAccountTestBuilder> {
        public:
            CreateAccountTestBuilder copy() override {
                return *this;
            }
            Operation buildOp() override;

            CreateAccountTestBuilder setFromAccount(Account from);

            CreateAccountTestBuilder setToPublicKey(PublicKey to);

            CreateAccountTestBuilder setType(AccountType accountType);

            CreateAccountTestBuilder setType(int32_t accountType);

            CreateAccountTestBuilder setSigner(Account *signer);

            CreateAccountTestBuilder setReferrer(AccountID *referrer);

            CreateAccountTestBuilder setPolicies(int32 policies);

            CreateAccountTestBuilder setPolicies(AccountPolicies policies);

            CreateAccountTestBuilder setResultCode(CreateAccountResultCode expectedResult);

            PublicKey to;
            AccountType accountType;
            AccountID *referrer = nullptr;
            int32 policies = -1;
            CreateAccountResultCode expectedResult = CreateAccountResultCode::SUCCESS;
        };

        template <typename Builder>
        class OperationChecker {
        public:
            static void applyAndCheckFirstOperation(OperationBuilder<Builder>* operationBuilder,
                                                    TestManager::pointer testManager){
                auto tx = operationBuilder->buildTx(testManager);
                applyCheck(tx, testManager->getLedgerDelta(), testManager->getApp());
                auto actualResult = getFirstResult(*tx).code();
                mustEqualsResultCode<OperationResultCode>(operationBuilder->operationResultCode, actualResult);
            }
        };

        class CreateAccountChecker : public OperationChecker<CreateAccountTestBuilder> {
        public:
            TestManager::pointer mTestManager;

            explicit CreateAccountChecker(TestManager::pointer testManager);

            void doCheck(CreateAccountTestBuilder builder,
                         CreateAccountResultCode actualResultCode);
        };

        class CreateAccountTestHelper : public TxHelper {
        public:
            friend CreateAccountChecker;

            explicit CreateAccountTestHelper(TestManager::pointer testManager);

            CreateAccountResultCode applyTx(CreateAccountTestBuilder builder);

            [[deprecated]]
            CreateAccountResultCode applyCreateAccountTx(Account &from, PublicKey to, AccountType accountType,
                                                         Account* signer = nullptr, AccountID *referrer = nullptr,
                                                         int32 policies = -1,
                                                         CreateAccountResultCode expectedResult = CreateAccountResultCode::SUCCESS);

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
