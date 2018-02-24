#include "StateBeforeTxHelper.h"

namespace stellar
{
namespace txtest
{
    StateBeforeTxHelper::StateBeforeTxHelper(const LedgerDelta::KeyEntryMap state)
    {
        mState = state;
    }

    SaleFrame::pointer StateBeforeTxHelper::getSale(const uint64_t id)
    {
        LedgerKey key;
        key.type(LedgerEntryType::SALE);
        key.sale().saleID = id;
        auto sale = mState.find(key);
        if (sale == mState.end())
        {
            return nullptr;
        }

        return std::make_shared<SaleFrame>(sale->second->mEntry);
    }

    AssetEntry StateBeforeTxHelper::getAssetEntry(AssetCode assetCode)
    {
        LedgerKey key;
        key.type(LedgerEntryType::ASSET);
        key.asset().code = assetCode;
        return mState[key]->mEntry.data.asset();
    }

    BalanceFrame::pointer StateBeforeTxHelper::getBalance(BalanceID balanceID) {
        LedgerKey key;
        key.type(LedgerEntryType::BALANCE);
        key.balance().balanceID = balanceID;
        if (mState.find(key) == mState.end())
            return nullptr;
        return std::make_shared<BalanceFrame>(mState[key]->mEntry);
    }

    OfferEntry StateBeforeTxHelper::getOffer(uint64_t offerID, AccountID ownerID)
    {
        LedgerKey key;
        key.type(LedgerEntryType::OFFER_ENTRY);
        key.offer().offerID = offerID;
        key.offer().ownerID = ownerID;
        return mState[key]->mEntry.data.offer();
    }

    std::vector<OfferEntry> StateBeforeTxHelper::getAllOffers()
    {
        std::vector<OfferEntry> offers;
        for (auto entryPair : mState)
        {
            const auto& ledgerEntry = entryPair.second->mEntry;
            if (ledgerEntry.data.type() == LedgerEntryType::OFFER_ENTRY)
                offers.push_back(ledgerEntry.data.offer());
        }
        return offers;
    }

    AccountFrame::pointer StateBeforeTxHelper::getAccount(AccountID accountID)
    {
        LedgerKey key;
        key.type(LedgerEntryType::ACCOUNT);
        key.account().accountID = accountID;

        if (mState.find(key) == mState.end())
            return nullptr;

        return std::make_shared<AccountFrame>(mState[key]->mEntry);
    }
}
}