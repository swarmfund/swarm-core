#ifndef STELLAR_CREATEACCOUNTTESTHELPER_H
#define STELLAR_CREATEACCOUNTTESTHELPER_H

#include "TxHelper.h"
#include "transactions/test/TxTests.h"

namespace stellar {
    namespace txtest {


        class CreateAccountTestBuilder : public OperationBuilder<CreateAccountTestBuilder> {
        public:
            CreateAccountTestBuilder copy() override {
                return *this;
            }

            Operation buildOp() override;

            CreateAccountTestBuilder setToPublicKey(PublicKey to);

            CreateAccountTestBuilder setRecovery(const PublicKey& recovery);

            CreateAccountTestBuilder setType(AccountType accountType);

            CreateAccountTestBuilder setType(int32_t accountType);

            CreateAccountTestBuilder setReferrer(AccountID *referrer);

            CreateAccountTestBuilder setPolicies(int32 policies);

            CreateAccountTestBuilder setPolicies(AccountPolicies policies);

            CreateAccountTestBuilder setRoleID(uint64_t roleID);

            CreateAccountTestBuilder setResultCode(CreateAccountResultCode expectedResult);

            PublicKey to;
            PublicKey recovery;
            AccountType accountType = AccountType::GENERAL;
            AccountID *referrer = nullptr;
            int32 policies = -1;
            uint64_t roleID = 0;
            bool isRoleIDSpecified = false;
            CreateAccountResultCode expectedResult = CreateAccountResultCode::SUCCESS;
        };

        // A - is multiplicity. Generate pair between each element.
        // B - is optional multiplicity. A * B. If B is not empty
        template <typename T>
        std::vector<std::pair<T, T>> combineElements(std::vector<T> A, std::vector<T> B = std::vector<T>()) {
            std::vector<std::pair<T, T>> G;
            for (int i = 0; i < A.size(); i++) {
                for (int j = 0; j < A.size(); j++) {
                    if (A[i] == A[j]) {
                        continue;
                    }
                    G.emplace_back(A[i], A[j]);
                }
            }
            if (B.empty()) {
                return G;
            }
            for (auto a : A) {
                for (auto b : B) {
                    G.emplace_back(a, b);
                }
            }
            return G;
        }

        class CreateAccountChecker {
        public:
            TestManager::pointer mTestManager;

            explicit CreateAccountChecker(TestManager::pointer testManager);

            void doCheck(CreateAccountTestBuilder builder, TransactionFramePtr txFrame);
        };

        class CreateAccountTestHelper : public TxHelper {
        public:
            friend CreateAccountChecker;

            explicit CreateAccountTestHelper(TestManager::pointer testManager);

            TransactionFramePtr createCreateAccountTx(Account& source, PublicKey to, AccountType accountType, uint32_t policies = 0);

            CreateAccountResultCode applyTx(CreateAccountTestBuilder builder);

            [[deprecated]]
            CreateAccountResultCode applyCreateAccountTx(Account &from, PublicKey to, AccountType accountType,
                                                         Account *signer = nullptr, AccountID *referrer = nullptr,
                                                         int32 policies = -1,
                                                         CreateAccountResultCode expectedResult = CreateAccountResultCode::SUCCESS);

        private:
            Account from;
            PublicKey to;
            PublicKey recovery;
            AccountType accountType = AccountType::GENERAL;
            Account *signer = nullptr;
            AccountID *referrer = nullptr;
            int32 policies = -1;
            CreateAccountResultCode expectedResult = CreateAccountResultCode::SUCCESS;
        };
    }

}


#endif //STELLAR_CREATEACCOUNTTESTHELPER_H
