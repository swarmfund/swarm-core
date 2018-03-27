#pragma once


#include "overlay/StellarXDR.h"
#include "TxHelper.h"
#include "ledger/SaleFrame.h"

namespace stellar
{
namespace txtest
{
    class StateBeforeTxHelper
    {
        LedgerDelta::KeyEntryMap mState;
    public:
        StateBeforeTxHelper();
        explicit StateBeforeTxHelper(LedgerDelta::KeyEntryMap state);

        AccountFrame::pointer getAccount(AccountID accountID);
        SaleFrame::pointer getSale(uint64_t id);
        AssetEntry getAssetEntry(AssetCode assetCode);
        AssetFrame::pointer getAssetFrame(AssetCode assetCode);
        OfferEntry getOffer(uint64_t offerID, AccountID ownerID);
        BalanceFrame::pointer getBalance(BalanceID balanceID);
        std::vector<OfferEntry> getAllOffers();
    };
}
}
