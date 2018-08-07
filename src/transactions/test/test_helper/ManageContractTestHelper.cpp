#include <transactions/ManageContractOpFrame.h>
#include "ManageContractTestHelper.h"
#include "ledger/ReviewableRequestHelper.h"
#include "test/test_marshaler.h"

namespace stellar
{
namespace txtest
{
ManageContractTestHelper::ManageContractTestHelper(TestManager::pointer testManager) : TxHelper(testManager)
{
}

TransactionFramePtr
ManageContractTestHelper::createManageContractTx(Account &source, ManageContractOp manageContractOp)
{
    Operation op;
    op.body.type(OperationType::MANAGE_CONTRACT);
    op.body.manageContractOp() = manageContractOp;

    return TxHelper::txFromOperation(source, op, nullptr);
}

ManageContractOp
ManageContractTestHelper::createAddDetailsOp(Account &source, uint64_t &contractID,
                                             longstring &details)
{
    ManageContractOp result;
    result.contractID = contractID;
    result.data.action(ManageContractAction::ADD_DETAILS);
    result.data.details() = details;

    return result;
}

ManageContractOp
ManageContractTestHelper::createConfirmOp(Account &source, uint64_t &contractID)
{
    ManageContractOp result;
    result.contractID = contractID;
    result.data.action(ManageContractAction::CONFIRM_COMPLETED);

    return result;
}

ManageContractOp
ManageContractTestHelper::createStartDisputeOp(Account &source, uint64_t &contractID,
                                               longstring &disputeReason) {}

ManageContractResult
ManageContractTestHelper::applyManageContractTx(Account &source, ManageContractOp manageContractOp,
                                                ManageContractResultCode expectedResult)
{
    Database& db = mTestManager->getDB();

    auto txFrame = createManageContractTx(source, manageContractOp);
    mTestManager->applyCheck(txFrame);

    auto result = txFrame->getResult().result.results()[0];
    REQUIRE(ManageContractOpFrame::getInnerCode(result) == expectedResult);

    return result.tr().manageContractResult();
}
}
}
