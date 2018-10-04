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

    ManageLimitsOp ManageLimitsTestHelper::createManageLimitsOp(AssetCode asset,
                        StatsOpType type, bool isConvertNeeded, uint64_t daily,
                        uint64_t weekly, uint64_t monthly, uint64_t annual,
                        xdr::pointer<AccountID> accountID,
                        xdr::pointer<AccountType> accountType)
    {
        ManageLimitsOp manageLimitsOp;
        manageLimitsOp.details.action(ManageLimitsAction::CREATE);
        manageLimitsOp.details.limitsCreateDetails().accountID = accountID;
        manageLimitsOp.details.limitsCreateDetails().accountType = accountType;
        manageLimitsOp.details.limitsCreateDetails().assetCode = asset;
        manageLimitsOp.details.limitsCreateDetails().statsOpType = type;
        manageLimitsOp.details.limitsCreateDetails().isConvertNeeded = isConvertNeeded;
        manageLimitsOp.details.limitsCreateDetails().dailyOut = daily;
        manageLimitsOp.details.limitsCreateDetails().weeklyOut = weekly;
        manageLimitsOp.details.limitsCreateDetails().monthlyOut = monthly;
        manageLimitsOp.details.limitsCreateDetails().annualOut = annual;

        return manageLimitsOp;
    }

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
             ManageLimitsAction::REMOVE)
             return;

         REQUIRE(txFrame->getResult().result.results()[0].tr().manageLimitsResult().success().details.id() != 0);
     }
}
}