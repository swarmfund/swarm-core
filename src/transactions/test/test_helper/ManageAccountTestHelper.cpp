//
// Created by volodymyr on 26.01.18.
//

#include <ledger/AccountHelper.h>
#include <transactions/OperationFrame.h>
#include <transactions/ManageAccountOpFrame.h>
#include "ManageAccountTestHelper.h"
#include "test/test_marshaler.h"
#include "CheckSaleStateTestHelper.h"

namespace stellar
{

namespace txtest
{

    ManageAccountTestHelper::ManageAccountTestHelper(TestManager::pointer testManager) : TxHelper(testManager)
    {

    }

    ManageAccountResult
    ManageAccountTestHelper::applyManageAccount(Account &root, AccountID destination, AccountType accountType, Reasons toAdd, Reasons toRemove,
                                                    ManageAccountResultCode expectedResult)
    {
        Database& db = mTestManager->getDB();
        auto txFrame = createManageAccountTx(root, destination, accountType, toAdd, toRemove);

        std::vector<LedgerDelta::KeyEntryMap> stateBeforeOps;
        mTestManager->applyCheck(txFrame, stateBeforeOps);

        auto opRes = txFrame->getResult().result.results()[0];
        REQUIRE(opRes.code() == OperationResultCode::opINNER);

        auto actualResult = ManageAccountOpFrame::getInnerCode(opRes);
        REQUIRE(actualResult == expectedResult);

        if (actualResult != ManageAccountResultCode::SUCCESS)
            return opRes.tr().manageAccountResult();

        StateBeforeTxHelper stateHelper(stateBeforeOps[0]);
        auto accountBeforeTx = stateHelper.getAccount(destination);
        REQUIRE(accountBeforeTx);
        auto accountAfterTx = AccountHelper::Instance()->loadAccount(destination, db);

        uint32_t expectedBlockReasons = (accountBeforeTx->getBlockReasons() | sumBlockReasons(toAdd)) & ~sumBlockReasons(toRemove);
        REQUIRE(accountAfterTx->getBlockReasons() == expectedBlockReasons);

        return opRes.tr().manageAccountResult();
    }

    TransactionFramePtr
    ManageAccountTestHelper::createManageAccountTx(Account root, AccountID destination, AccountType accountType, Reasons toAdd, Reasons toRemove)
    {
        Operation op;
        op.body.type(OperationType::MANAGE_ACCOUNT);
        auto& manageAccountOp = op.body.manageAccountOp();

        manageAccountOp.account = destination;
        manageAccountOp.accountType = accountType;

        manageAccountOp.blockReasonsToAdd = sumBlockReasons(toAdd);
        manageAccountOp.blockReasonsToRemove = sumBlockReasons(toRemove);

        return TxHelper::txFromOperation(root, op);
    }

    uint32_t ManageAccountTestHelper::sumBlockReasons(const ManageAccountTestHelper::Reasons &reasons)
    {
        uint32_t logicSum(0);
        for (auto reason : reasons)
        {
            logicSum |= static_cast<uint32_t>(reason);
        }

        return logicSum;
    }

}

}
