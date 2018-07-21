#pragma once

#include "TxHelper.h"

namespace stellar
{
namespace txtest
{
class ManageInvoiceRequestTestHelper : TxHelper
{
public:
    explicit ManageInvoiceRequestTestHelper(TestManager::pointer testManager);


    TransactionFramePtr createManageInvoiceRequest(Account& source, ManageInvoiceRequestOp& manageInvoiceRequestOp);

    ManageInvoiceRequestResult applyManageInvoiceRequest(Account& source,
                                                         ManageInvoiceRequestOp& manageInvoiceRequestOp,
                          ManageInvoiceRequestResultCode expectedResult = ManageInvoiceRequestResultCode ::SUCCESS);


    ManageInvoiceRequestOp createInvoiceRequest(AssetCode asset, AccountID sender,
                                                uint64_t amount, longstring details);

    ManageInvoiceRequestOp createRemoveInvoiceRequest(uint64_t& requestID);

};

}
}