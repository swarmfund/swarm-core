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
                createAccountOp.policies = static_cast<uint32_t>(policies);
            if (referrer)
                createAccountOp.referrer.activate() = *referrer;

            return TxHelper::txFromOperation(from, op, signer);
        }

        CreateAccountResultCode CreateAccountTestHelper::applyCreateAccountTx(CreateAccountResultCode expectedResult) {
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
    }
}


