#pragma once

#include "StateBeforeTxHelper.h"
#include "ManageOfferTestHelper.h"
#include "ledger/SaleFrame.h"

namespace stellar
{
namespace txtest
{
    class CheckSaleStateHelper : public TxHelper
    {
        void ensureCancel(CheckSaleStateSuccess result, StateBeforeTxHelper& stateBeforeTx) const;
        void ensureClose(CheckSaleStateSuccess result, StateBeforeTxHelper& stateBeforeTx) const;
        void ensureNoOffersLeft(CheckSaleStateSuccess result, StateBeforeTxHelper& stateBeforeTx) const;
        void checkBalancesAfterApproval(StateBeforeTxHelper& stateBeforeTx, SaleFrame::pointer sale,
            SaleQuoteAsset const& saleQuoteAsset, CheckSubSaleClosedResult result) const;
    public:
        explicit CheckSaleStateHelper(TestManager::pointer testManager);

        TransactionFramePtr createCheckSaleStateTx(Account& source, uint64_t saleID);
        CheckSaleStateResult applyCheckSaleStateTx(Account& source, uint64_t saleID, CheckSaleStateResultCode code = CheckSaleStateResultCode::SUCCESS);

    };
}

}
