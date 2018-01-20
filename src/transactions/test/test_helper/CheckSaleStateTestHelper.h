#ifndef STELLAR_CHECK_SALE_STATE_TEST_HELPER_H
#define STELLAR_CHECK_SALE_STATE_TEST_HELPER_H

#include "ManageOfferTestHelper.h"
#include "ledger/SaleFrame.h"

namespace stellar
{
namespace txtest
{
    class StateBeforeTxHelper
    {
        LedgerDelta::KeyEntryMap mState;
    public:
        explicit StateBeforeTxHelper(LedgerDelta::KeyEntryMap state);

        SaleFrame::pointer getSale(uint64_t id);
        AssetEntry getAssetEntry(AssetCode assetCode);
        OfferEntry getOffer(uint64_t offerID, AccountID ownerID);
        BalanceFrame::pointer getBalance(BalanceID balanceID);
        std::vector<OfferEntry> getAllOffers();
    };

    class CheckSaleStateHelper : public TxHelper
    {
        void ensureCancel(CheckSaleStateSuccess result, StateBeforeTxHelper& stateBeforeTx) const;
        void ensureClose(CheckSaleStateSuccess result, StateBeforeTxHelper& stateBeforeTx) const;
        void ensureNoOffersLeft(CheckSaleStateSuccess result, StateBeforeTxHelper& stateBeforeTx) const;
    public:
        explicit CheckSaleStateHelper(TestManager::pointer testManager);

        TransactionFramePtr createCheckSaleStateTx(Account& source);
        CheckSaleStateResult applyCheckSaleStateTx(Account& source, CheckSaleStateResultCode code = CheckSaleStateResultCode::SUCCESS);

    };
}

}


#endif //STELLAR_CHECK_SALE_STATE_TEST_HELPER_H
