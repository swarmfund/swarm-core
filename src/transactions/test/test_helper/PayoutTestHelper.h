#pragma once

#include "overlay/StellarXDR.h"
#include "TxHelper.h"

namespace stellar {
namespace txtest {
    class PayoutTestHelper : TxHelper {
    public:
        explicit PayoutTestHelper(TestManager::pointer testManager);

        TransactionFramePtr
        createPayoutTx(Account &source, AssetCode asset, BalanceID sourceBalanceID,
                                           uint64_t maxPayoutAmount, uint64_t minPayOutAmount, Fee &fee);

        PayoutResult
        applyPayoutTx(Account &source, AssetCode asset,
                      BalanceID sourceBalanceID, uint64_t maxPayoutAmount,
                      uint64_t minPayOutAmount, Fee &fee,
                      PayoutResultCode expectedResult = PayoutResultCode::SUCCESS);
    };
}
}