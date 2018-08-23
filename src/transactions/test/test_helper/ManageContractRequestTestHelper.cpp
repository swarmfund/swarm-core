#include <ledger/ReviewableRequestHelper.h>
#include <transactions/ManageContractRequestOpFrame.h>
#include <lib/catch.hpp>
#include "ManageContractRequestTestHelper.h"


namespace stellar
{
namespace txtest
{

ManageContractRequestTestHelper::ManageContractRequestTestHelper(TestManager::pointer testManager)
        : TxHelper(testManager)
{
}

ManageContractRequestOp
ManageContractRequestTestHelper::createContractRequest(AccountID customer, AccountID escrow,
                                                       uint64_t startTime, uint64_t endTime,
                                                       longstring details)
{
    ManageContractRequestOp result;
    result.details.action(ManageContractRequestAction::CREATE);
    result.details.contractRequest().customer = customer;
    result.details.contractRequest().escrow = escrow;
    result.details.contractRequest().startTime = startTime;
    result.details.contractRequest().endTime = endTime;
    result.details.contractRequest().details = details;
    result.details.contractRequest().ext.v(LedgerVersion::EMPTY_VERSION);
    result.ext.v(LedgerVersion::EMPTY_VERSION);

    return result;
}

ManageContractRequestOp
ManageContractRequestTestHelper::createRemoveContractRequest(uint64_t& requestID)
{
    ManageContractRequestOp result;
    result.details.action(ManageContractRequestAction::REMOVE);
    result.details.requestID() = requestID;
    result.ext.v(LedgerVersion::EMPTY_VERSION);

    return result;
}

TransactionFramePtr
ManageContractRequestTestHelper::createManageContractRequest(Account& source,
                                                             ManageContractRequestOp& manageContractRequestOp)
{
    Operation op;
    op.body.type(OperationType::MANAGE_CONTRACT_REQUEST);
    op.body.manageContractRequestOp() = manageContractRequestOp;

    return txFromOperation(source, op, nullptr);
}

ManageContractRequestResult
ManageContractRequestTestHelper::applyManageContractRequest(Account& source,
                                                            ManageContractRequestOp& manageContractRequestOp,
                                                            ManageContractRequestResultCode expectedResult)
{
    Database& db = mTestManager->getDB();

    auto reviewableRequestHelper = ReviewableRequestHelper::Instance();
    uint64 reviewableRequestCountBeforeTx = reviewableRequestHelper->countObjects(db.getSession());

    auto txFrame = createManageContractRequest(source, manageContractRequestOp);
    mTestManager->applyCheck(txFrame);
    auto txResult = txFrame->getResult();
    auto opResult = txResult.result.results()[0];

    auto actualResult = ManageContractRequestOpFrame::getInnerCode(opResult);
    REQUIRE(actualResult == expectedResult);

    uint64 reviewableRequestCountAfterTx = reviewableRequestHelper->countObjects(db.getSession());
    if (expectedResult != ManageContractRequestResultCode::SUCCESS)
    {
        REQUIRE(reviewableRequestCountBeforeTx == reviewableRequestCountAfterTx);
        return ManageContractRequestResult{};
    }

    auto manageContractRequestResult = opResult.tr().manageContractRequestResult();
    switch (manageContractRequestResult.success().details.action())
    {
        case ManageContractRequestAction::CREATE:
        {
            auto contractRequest = reviewableRequestHelper->loadRequest(
                    manageContractRequestResult.success().details.response().requestID, db);
            REQUIRE(!!contractRequest);
            REQUIRE(reviewableRequestCountBeforeTx + 1 == reviewableRequestCountAfterTx);
            break;
        }
        case ManageContractRequestAction::REMOVE:
        {
            REQUIRE(reviewableRequestCountBeforeTx == reviewableRequestCountAfterTx +1);
            break;
        }
        default:
            throw std::runtime_error("Unexpected ManageContractRequestAction in tests");
    }

    return manageContractRequestResult;
}

}
}
