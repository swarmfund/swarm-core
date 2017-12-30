#include <ledger/AccountHelper.h>
#include <transactions/CreateAccountOpFrame.h>
#include <ledger/StatisticsHelper.h>
#include <ledger/BalanceHelper.h>
#include "CreateAccountTestHelper.h"
#include "test/test_marshaler.h"

namespace stellar
{
namespace txtest
{

    CreateAccountTestHelper::CreateAccountTestHelper(TestManager::pointer testManager) : TxHelper(testManager)
    {
    }

    TransactionFramePtr CreateAccountTestHelper::createCreateAccountTx(Account &from, PublicKey &to, AccountType accountType,
                                                                       Account* signer, AccountID* referrer, int32 policies)
    {
        Operation op;
        op.body.type(OperationType::CREATE_ACCOUNT);
        CreateAccountOp& createAccountOp = op.body.createAccountOp();
        createAccountOp.accountType = accountType;
        createAccountOp.destination = to;

        if (policies != -1)
            createAccountOp.policies = static_cast<uint32_t>(policies);
        if (referrer)
            createAccountOp.referrer.activate() = *referrer;

        return TxHelper::txFromOperation(from, op, signer);
    }

    CreateAccountResultCode CreateAccountTestHelper::applyCreateAccountTx(Account &from, PublicKey to, AccountType accountType,
                                                                          Account *signer, AccountID *referrer,
                                                                          int32 policies, CreateAccountResultCode expectedResult)
    {
        Database& db = mTestManager->getDB();

        auto accountHelper = AccountHelper::Instance();
        AccountFrame::pointer fromAccount = accountHelper->loadAccount(from.key.getPublicKey(), db);
        AccountFrame::pointer toAccount = accountHelper->loadAccount(to, db);

        TransactionFramePtr txFrame = createCreateAccountTx(from, to, accountType, signer, referrer, policies);
        mTestManager->applyCheck(txFrame);
        auto txResult = txFrame->getResult();
        auto opResult = txResult.result.results()[0];
        auto actualResultCode = CreateAccountOpFrame::getInnerCode(opResult);
        REQUIRE(actualResultCode == expectedResult);

        REQUIRE(txResult.feeCharged == mTestManager->getApp().getLedgerManager().getTxFee());

        AccountFrame::pointer toAccountAfter = accountHelper->loadAccount(to, db);

        if (actualResultCode != CreateAccountResultCode::SUCCESS)
        {
            // check that the target account didn't change
            REQUIRE(!!toAccount == !!toAccountAfter);
            if (toAccount && toAccountAfter)
            {
                REQUIRE(toAccount->getAccount() == toAccountAfter->getAccount());
            }

            return actualResultCode;
        }
        REQUIRE(toAccountAfter);
        REQUIRE(!toAccountAfter->isBlocked());
        REQUIRE(toAccountAfter->getAccountType() == accountType);

        auto statisticsHelper = StatisticsHelper::Instance();
        auto statisticsFrame = statisticsHelper->loadStatistics(to, db);
        REQUIRE(statisticsFrame);
        auto statistics = statisticsFrame->getStatistics();
        REQUIRE(statistics.dailyOutcome == 0);
        REQUIRE(statistics.weeklyOutcome == 0);
        REQUIRE(statistics.monthlyOutcome == 0);
        REQUIRE(statistics.annualOutcome == 0);

        if (!toAccount)
        {
            auto balanceHelper = BalanceHelper::Instance();
            std::vector<BalanceFrame::pointer> balances;
            balanceHelper->loadBalances(toAccountAfter->getAccount().accountID, balances, db);
            for (const auto& balance : balances)
            {
                REQUIRE(balance->getBalance().amount == 0);
                REQUIRE(balance->getAccountID() == toAccountAfter->getAccount().accountID);
            }
        }

        return actualResultCode;
    }


    }
    }


