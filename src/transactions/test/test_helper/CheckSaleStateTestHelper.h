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

        AccountFrame::pointer getAccount(AccountID accountID);
        SaleFrame::pointer getSale(uint64_t id);
        AssetEntry getAssetEntry(AssetCode assetCode);
        OfferEntry getOffer(uint64_t offerID, AccountID ownerID);
        BalanceFrame::pointer getBalance(BalanceID balanceID);
        std::vector<OfferEntry> getAllOffers();
    };

    class CheckSaleStateHelper : public TxHelper
    {
        void ensureClose(CheckSaleStateSuccess result, StateBeforeTxHelper& stateBeforeTx) const;
        void ensureUpdated(CheckSaleStateSuccess result, StateBeforeTxHelper& stateBeforeTx) const;
        void ensureNoOffersLeft(CheckSaleStateSuccess result, StateBeforeTxHelper& stateBeforeTx) const;
        void checkBalancesAfterApproval(StateBeforeTxHelper& stateBeforeTx, SaleFrame::pointer sale,
            SaleQuoteAsset const& saleQuoteAsset, CheckSubSaleClosedResult result) const;
    public:
        explicit CheckSaleStateHelper(TestManager::pointer testManager);

        void ensureCancel(uint64_t saleID, StateBeforeTxHelper &stateBeforeTx) const;
        TransactionFramePtr createCheckSaleStateTx(Account& source, uint64_t saleID);
        CheckSaleStateResult applyCheckSaleStateTx(Account& source, uint64_t saleID, CheckSaleStateResultCode code = CheckSaleStateResultCode::SUCCESS);

    };
}

}


#endif //STELLAR_CHECK_SALE_STATE_TEST_HELPER_H
