#include <ledger/ReviewableRequestHelper.h>
#include <transactions/ManageInvoiceRequestOpFrame.h>
#include <lib/catch.hpp>
#include "ManageInvoiceRequestTestHelper.h"


namespace stellar
{
namespace txtest
{

ManageInvoiceRequestTestHelper::ManageInvoiceRequestTestHelper(TestManager::pointer testManager)
        : TxHelper(testManager)
{
}

ManageInvoiceRequestOp
ManageInvoiceRequestTestHelper::createInvoiceRequest(AssetCode asset, AccountID sender,
                                                     uint64_t amount, longstring details, uint64_t* contractID)
{
    ManageInvoiceRequestOp result;
    result.details.action(ManageInvoiceRequestAction::CREATE);
    result.details.invoiceRequest().asset = asset;
    result.details.invoiceRequest().sender = sender;
    result.details.invoiceRequest().amount = amount;
    result.details.invoiceRequest().details = details;

    if (contractID != nullptr)
        result.details.invoiceRequest().contractID.activate() = *contractID;

    result.details.invoiceRequest().ext.v(LedgerVersion::EMPTY_VERSION);
    result.ext.v(LedgerVersion::EMPTY_VERSION);

    return result;
}

ManageInvoiceRequestOp
ManageInvoiceRequestTestHelper::createRemoveInvoiceRequest(uint64_t& requestID)
{
    ManageInvoiceRequestOp result;
    result.details.action(ManageInvoiceRequestAction::REMOVE);
    result.details.requestID() = requestID;
    result.ext.v(LedgerVersion::EMPTY_VERSION);

    return  result;
}

TransactionFramePtr
ManageInvoiceRequestTestHelper::createManageInvoiceRequest(Account& source,
                                                           ManageInvoiceRequestOp& manageInvoiceRequestOp)
{
    Operation op;
    op.body.type(OperationType::MANAGE_INVOICE_REQUEST);
    op.body.manageInvoiceRequestOp() = manageInvoiceRequestOp;

    return txFromOperation(source, op, nullptr);
}

ManageInvoiceRequestResult
ManageInvoiceRequestTestHelper::applyManageInvoiceRequest(Account& source,
                                                          ManageInvoiceRequestOp& manageInvoiceRequestOp,
                                                          ManageInvoiceRequestResultCode expectedResult)
{
    Database& db = mTestManager->getDB();

    auto reviewableRequestHelper = ReviewableRequestHelper::Instance();
    uint64 reviewableRequestCountBeforeTx = reviewableRequestHelper->countObjects(db.getSession());

    auto txFrame = createManageInvoiceRequest(source, manageInvoiceRequestOp);
    mTestManager->applyCheck(txFrame);
    auto txResult = txFrame->getResult();
    auto opResult = txResult.result.results()[0];

    auto actualResult = ManageInvoiceRequestOpFrame::getInnerCode(opResult);
    REQUIRE(actualResult == expectedResult);

    uint64 reviewableRequestCountAfterTx = reviewableRequestHelper->countObjects(db.getSession());
    if (expectedResult != ManageInvoiceRequestResultCode::SUCCESS)
    {
        REQUIRE(reviewableRequestCountBeforeTx == reviewableRequestCountAfterTx);
        return ManageInvoiceRequestResult{};
    }

    auto manageInvoiceRequestResult = opResult.tr().manageInvoiceRequestResult();
    switch (manageInvoiceRequestResult.success().details.action())
    {
        case ManageInvoiceRequestAction::CREATE:
        {
            auto invoiceRequest = reviewableRequestHelper->loadRequest(
                    manageInvoiceRequestResult.success().details.response().requestID, db);
            REQUIRE(!!invoiceRequest);
            REQUIRE(reviewableRequestCountBeforeTx + 1 == reviewableRequestCountAfterTx);
            break;
        }
        case ManageInvoiceRequestAction::REMOVE:
        {
            REQUIRE(reviewableRequestCountBeforeTx == reviewableRequestCountAfterTx +1);
            break;
        }
        default:
            throw std::runtime_error("Unexpected ManageInvoiceRequestAction in tests");
    }

    return manageInvoiceRequestResult;
}

}
}