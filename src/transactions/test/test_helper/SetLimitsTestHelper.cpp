#include "SetLimitsTestHelper.h"
#include "transactions/ManageLimitsOpFrame.h"
#include "test/test_marshaler.h"

namespace stellar
{
namespace txtest
{
    SetLimitsTestHelper::SetLimitsTestHelper(TestManager::pointer testManager) : TxHelper(testManager)
    {
    }

    TransactionFramePtr
    SetLimitsTestHelper::createSetLimitsTx(Account &source, AccountID *account, AccountType *accountType,
                                           Limits limits)
    {
        Operation op;
        op.body.type(OperationType::SET_LIMITS);

        ManageLimitsOp& setLimitsOp = op.body.manageLimitsOp();

        if (account)
            setLimitsOp.account.activate() = *account;
        if (accountType)
            setLimitsOp.accountType.activate() = *accountType;
        setLimitsOp.limits = limits;

        return TxHelper::txFromOperation(source, op, nullptr);
    }

    void
    SetLimitsTestHelper::applySetLimitsTx(Account &source, AccountID *account, AccountType *accountType,
                                          Limits limits, ManageLimitsResultCode expectedResult)
    {
        TransactionFramePtr txFrame;
        txFrame = createSetLimitsTx(source, account, accountType, limits);
        mTestManager->applyCheck(txFrame);
        REQUIRE(ManageLimitsOpFrame::getInnerCode(
                txFrame->getResult().result.results()[0]) == expectedResult);
    }

}
}