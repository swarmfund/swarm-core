#pragma once

#include "ledger/AccountHelper.h"
#include "ledger/AssetHelper.h"
#include "ledger/BalanceHelper.h"
#include "ledger/LedgerDelta.h"
#include "ledger/OfferHelper.h"
#include "ledger/SaleHelper.h"

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
}
}


