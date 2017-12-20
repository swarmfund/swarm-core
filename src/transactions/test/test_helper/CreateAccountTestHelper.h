#ifndef STELLAR_CREATEACCOUNTTESTHELPER_H
#define STELLAR_CREATEACCOUNTTESTHELPER_H

#include "TxHelper.h"

namespace stellar {
    namespace txtest {

        class CreateAccountTestHelper;

        class CreateAccountChecker {
        public:
            TestManager::pointer mTestManager;

            explicit CreateAccountChecker(TestManager::pointer testManager) : mTestManager(testManager) {
            }

            void doCheck(CreateAccountTestHelper *testHelper,
                         CreateAccountResultCode actualResultCode) {
                Database &db = mTestManager->getDB();
                auto accountHelper = AccountHelper::Instance();
                AccountFrame::pointer toAccount = accountHelper->loadAccount(testHelper->to, db);
                AccountFrame::pointer toAccountAfter = accountHelper->loadAccount(testHelper->to, db);

                if (actualResultCode != CreateAccountResultCode::SUCCESS) {
                    // check that the target account didn't change
                    REQUIRE(!!toAccount == !!toAccountAfter);
                    if (toAccount && toAccountAfter) {
                        REQUIRE(toAccount->getAccount() == toAccountAfter->getAccount());
                    }
                    return;
                }

                REQUIRE(toAccountAfter);
                REQUIRE(!toAccountAfter->isBlocked());
                REQUIRE(toAccountAfter->getAccountType() == testHelper->accountType);

                auto statisticsFrame = StatisticsHelper::Instance()->loadStatistics(testHelper->to, db);
                REQUIRE(statisticsFrame);
                auto statistics = statisticsFrame->getStatistics();
                REQUIRE(statistics.dailyOutcome == 0);
                REQUIRE(statistics.weeklyOutcome == 0);
                REQUIRE(statistics.monthlyOutcome == 0);
                REQUIRE(statistics.annualOutcome == 0);

                if (!toAccount) {
                    std::vector<BalanceFrame::pointer> balances;
                    BalanceHelper::Instance()->loadBalances(toAccountAfter->getAccount().accountID, balances, db);
                    for (const auto &balance : balances) {
                        REQUIRE(balance->getBalance().amount == 0);
                        REQUIRE(balance->getAccountID() == toAccountAfter->getAccount().accountID);
                    }
                }
            }
        };

        class CreateAccountTestHelper : TxHelper {
        public:
            friend CreateAccountChecker;

            explicit CreateAccountTestHelper(TestManager::pointer testManager);

            CreateAccountResultCode applyCreateAccountTx(CreateAccountResultCode expectedResult
                = CreateAccountResultCode::SUCCESS);

            TransactionFramePtr createCreateAccountTx();

            CreateAccountTestHelper setFromAccount(Account from) {
                auto newTestHelper = *this;
                newTestHelper.from = from;
                return newTestHelper;
            }

            CreateAccountTestHelper setToPublicKey(PublicKey to) {
                auto newTestHelper = *this;
                newTestHelper.to = to;
                return newTestHelper;
            }

            CreateAccountTestHelper setAccountType(AccountType accountType) {
                auto newTestHelper = *this;
                newTestHelper.accountType = accountType;
                return newTestHelper;
            }

            CreateAccountTestHelper setSignerAccount(Account* signer) {
                auto newTestHelper = *this;
                newTestHelper.signer = signer;
                return newTestHelper;
            }

            CreateAccountTestHelper setReferrerAccount(AccountID *referrer) {
                auto newTestHelper = *this;
                newTestHelper.referrer = referrer;
                return newTestHelper;
            }

            CreateAccountTestHelper setPolicies(int32 policies) {
                auto newTestHelper = *this;
                newTestHelper.policies = policies;
                return newTestHelper;
            }

            CreateAccountTestHelper setResultCode(CreateAccountResultCode expectedResult) {
                auto newTestHelper = *this;
                newTestHelper.expectedResult = expectedResult;
                return newTestHelper;
            }

        private:
            Account from;
            PublicKey to;
            AccountType accountType;
            Account *signer = nullptr;
            AccountID *referrer = nullptr;
            int32 policies = -1;
            CreateAccountResultCode expectedResult;
        };
    }

}


#endif //STELLAR_CREATEACCOUNTTESTHELPER_H
