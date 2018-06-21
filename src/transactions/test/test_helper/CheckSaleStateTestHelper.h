#pragma once

#include <ledger/SaleAnteFrame.h>
#include "StateBeforeTxHelper.h"
#include "ManageOfferTestHelper.h"
#include "ledger/SaleFrame.h"

namespace stellar
{
namespace txtest
{

    class CheckSaleStateHelper : public TxHelper
    {
        void ensureClose(CheckSaleStateSuccess result, StateBeforeTxHelper& stateBeforeTx,
                         std::unordered_map<BalanceID, SaleAnteFrame::pointer> saleAntesBeforeTx) const;
        void ensureUpdated(CheckSaleStateSuccess result, StateBeforeTxHelper& stateBeforeTx) const;
        void ensureNoOffersLeft(CheckSaleStateSuccess result, StateBeforeTxHelper& stateBeforeTx) const;
        void ensureNoSaleAntesLeft(uint64_t saleID) const;
        void checkBalancesAfterApproval(StateBeforeTxHelper& stateBeforeTx, SaleFrame::pointer sale,
                                        SaleQuoteAsset const& saleQuoteAsset, CheckSubSaleClosedResult result,
                                        std::unordered_map<BalanceID, SaleAnteFrame::pointer> saleAntesBeforeTx) const;
    public:
        explicit CheckSaleStateHelper(TestManager::pointer testManager);

        void ensureCancel(uint64_t saleID, StateBeforeTxHelper& stateBeforeTx,
                          std::unordered_map<BalanceID, SaleAnteFrame::pointer> saleAntesBeforeTx) const;

        TransactionFramePtr createCheckSaleStateTx(Account& source, uint64_t saleID);
        CheckSaleStateResult applyCheckSaleStateTx(Account& source, uint64_t saleID, CheckSaleStateResultCode code = CheckSaleStateResultCode::SUCCESS);

    };
}

}
