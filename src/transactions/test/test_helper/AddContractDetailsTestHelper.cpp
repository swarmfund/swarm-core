#include <transactions/ManageContractOpFrame.h>
#include "AddContractDetailsTestHelper.h"
#include "ledger/ReviewableRequestHelper.h"
#include "test/test_marshaler.h"

namespace stellar
{
namespace txtest
{
AddContractDetailsTestHelper::AddContractDetailsTestHelper(TestManager::pointer testManager) : TxHelper(testManager)
{
}

TransactionFramePtr
AddContractDetailsTestHelper::createAddContractDetailsTx(Account &source, uint64_t& contractID, longstring& details)
{
    Operation op;
    op.body.type(OperationType::ADD_CONTRACT_DETAILS);
    AddContractDetailsOp& addContractDetailsOp = op.body.addContractDetailsOp();

    addContractDetailsOp.contractID = contractID;
    addContractDetailsOp.details = details;

    return TxHelper::txFromOperation(source, op, nullptr);
}

void
AddContractDetailsTestHelper::applyAddContractDetailsTx(Account &source, uint64_t& contractID, longstring details,
                                                        AddContractDetailsResultCode expectedResult)
{
    Database& db = mTestManager->getDB();

    auto txFrame = createAddContractDetailsTx(source, contractID, details);
    mTestManager->applyCheck(txFrame);


    REQUIRE(AddContractDetailsOpFrame::getInnerCode(txFrame->getResult().result.results()[0]) == expectedResult);
}
}
}
