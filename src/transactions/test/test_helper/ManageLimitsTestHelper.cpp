//
// Created by artem on 08.06.18.
//

#include "ManageLimitsTestHelper.h"
#include "transactions/ManageLimitsOpFrame.h"
#include "test/test_marshaler.h"


namespace stellar
{
namespace txtest
{
    ManageLimitsTestHelper::ManageLimitsTestHelper(TestManager::pointer testManager) : TxHelper(testManager) {}

    TransactionFramePtr
    ManageLimitsTestHelper::createManageLimitsTx(Account &source, ManageLimitsOp& manageLimitsOp)
    {
        Operation op;
        op.body.type(OperationType::MANAGE_LIMITS);
        op.body.manageLimitsOp() = manageLimitsOp;

        return TxHelper::txFromOperation(source, op, nullptr);
    }

     void
     ManageLimitsTestHelper::applyManageLimitsTx(Account &source, ManageLimitsOp& manageLimitsOp, ManageLimitsResultCode expectedResult)
     {
         TransactionFramePtr txFrame;
         txFrame = createManageLimitsTx(source, manageLimitsOp);
         mTestManager->applyCheck(txFrame);
         REQUIRE(ManageLimitsOpFrame::getInnerCode(txFrame->getResult().result.results()[0]) == expectedResult);
         if (ManageLimitsOpFrame::getInnerCode(txFrame->getResult().result.results()[0]) !=
             ManageLimitsResultCode::SUCCESS)
             return;

         if (txFrame->getResult().result.results()[0].tr().manageLimitsResult().success().details.action() ==
             ManageLimitsAction::DELETE)
             return;

         REQUIRE(txFrame->getResult().result.results()[0].tr().manageLimitsResult().success().details.id() != 0);
     }
}
}