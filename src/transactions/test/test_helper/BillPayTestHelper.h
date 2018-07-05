#pragma once

#include "TxHelper.h"
#include "PaymentV2TestHelper.h"

namespace stellar
{
namespace txtest
{
class BillPayTestHelper : TxHelper
{
public:
    explicit BillPayTestHelper(TestManager::pointer testManager);

    TransactionFramePtr createBillPayTx(Account &source, uint64_t requestID, BalanceID sourceBalanceID,
                                          PaymentOpV2::_destination_t destination, uint64_t amount,
                                          PaymentFeeDataV2 feeData, std::string subject, std::string reference);

    BillPayResult applyBillPayTx(Account &source, uint64_t requestID, BalanceID sourceBalanceID,
                                     PaymentOpV2::_destination_t destination, uint64_t amount,
                                     PaymentFeeDataV2 feeData, std::string subject, std::string reference,
                                     PaymentV2Delta *paymentDelta = nullptr,
                                 BillPayResultCode expectedResultCode = BillPayResultCode::SUCCESS);


};
}
}