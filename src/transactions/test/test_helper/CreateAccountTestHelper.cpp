#include <ledger/AccountHelper.h>
#include <transactions/CreateAccountOpFrame.h>
#include <ledger/StatisticsHelper.h>
#include <ledger/BalanceHelper.h>
#include "CreateAccountTestHelper.h"

namespace stellar {
    namespace txtest {

        CreateAccountTestHelper::CreateAccountTestHelper(TestManager::pointer testManager) : TxHelper(testManager) {
        }

        TransactionFramePtr CreateAccountTestHelper::createCreateAccountTx() {
            Operation op;
            op.body.type(OperationType::CREATE_ACCOUNT);
            CreateAccountOp &createAccountOp = op.body.createAccountOp();
            createAccountOp.accountType = accountType;
            createAccountOp.destination = to;

            if (policies != -1)
                createAccountOp.policies = policies;
            if (referrer)
                createAccountOp.referrer.activate() = *referrer;

            return TxHelper::txFromOperation(from, op, signer);
        }

        CreateAccountResultCode CreateAccountTestHelper::applyTx() {
            TransactionFramePtr txFrame = createCreateAccountTx();
            mTestManager->applyCheck(txFrame);
            auto txResult = txFrame->getResult();
            auto opResult = txResult.result.results()[0];
            auto actualResultCode = CreateAccountOpFrame::getInnerCode(opResult);

            REQUIRE(actualResultCode == expectedResult);
            REQUIRE(txResult.feeCharged == mTestManager->getApp().getLedgerManager().getTxFee());

            auto checker = CreateAccountChecker(mTestManager);
            checker.doCheck(this, actualResultCode);
            return actualResultCode;
        }

        CreateAccountTestHelper CreateAccountTestHelper::setFromAccount(Account from) {
            auto newTestHelper = *this;
            newTestHelper.from = from;
            return newTestHelper;
        }

        CreateAccountTestHelper CreateAccountTestHelper::setToPublicKey(PublicKey to) {
            auto newTestHelper = *this;
            newTestHelper.to = to;
            return newTestHelper;
        }

        CreateAccountTestHelper CreateAccountTestHelper::setType(AccountType accountType) {
            auto newTestHelper = *this;
            newTestHelper.accountType = accountType;
            return newTestHelper;
        }

        CreateAccountTestHelper CreateAccountTestHelper::setType(int32_t accountType) {
            return setType(static_cast<AccountType>(accountType));
        }

        CreateAccountTestHelper CreateAccountTestHelper::setSigner(Account *signer) {
            auto newTestHelper = *this;
            newTestHelper.signer = signer;
            return newTestHelper;
        }

        CreateAccountTestHelper CreateAccountTestHelper::setReferrer(AccountID *referrer) {
            auto newTestHelper = *this;
            newTestHelper.referrer = referrer;
            return newTestHelper;
        }

        CreateAccountTestHelper CreateAccountTestHelper::setPolicies(int32 policies) {
            auto newTestHelper = *this;
            newTestHelper.policies = policies;
            return newTestHelper;
        }

        CreateAccountTestHelper CreateAccountTestHelper::setPolicies(AccountPolicies policies) {
            return setPolicies(static_cast<int32_t>(policies));
        }

        CreateAccountTestHelper CreateAccountTestHelper::setResultCode(CreateAccountResultCode expectedResult) {
            auto newTestHelper = *this;
            newTestHelper.expectedResult = expectedResult;
            return newTestHelper;
        }

        CreateAccountResultCode
        CreateAccountTestHelper::applyCreateAccountTx(Account &from, PublicKey to, AccountType accountType,
                                                      Account *signer, AccountID *referrer, int32 policies,
                                                      CreateAccountResultCode expectedResult) {
            return setFromAccount(from)
                    .setToPublicKey(to)
                    .setType(accountType)
                    .setSigner(signer)
                    .setReferrer(referrer)
                    .setPolicies(policies)
                    .setResultCode(expectedResult)
                    .applyTx();
        }

        void
        CreateAccountChecker::doCheck(CreateAccountTestHelper *testHelper, CreateAccountResultCode actualResultCode) {
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

        CreateAccountChecker::CreateAccountChecker(TestManager::pointer testManager) : mTestManager(testManager) {
        }
    }
}


