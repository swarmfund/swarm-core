#include "StateBeforeTxHelper.h"

#include "WithdrawRequestHelper.h"
#include "ledger/BalanceHelperLegacy.h"
#include "transactions/CreateWithdrawalRequestOpFrame.h"
#include "test/test_marshaler.h"


namespace stellar
{
namespace txtest
{

    StateBeforeTxHelper::StateBeforeTxHelper()
    {
    }

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
        auto assetFrame = getAssetFrame(assetCode);
        REQUIRE(!!assetFrame);
        return assetFrame->getAsset();
    }

AssetFrame::pointer StateBeforeTxHelper::getAssetFrame(AssetCode assetCode)
{
    LedgerKey key;
    key.type(LedgerEntryType::ASSET);
    key.asset().code = assetCode;
    auto entryFrame = mState.find(key);
    if (entryFrame == mState.end())
    {
        return nullptr;
    }

    return std::make_shared<AssetFrame>(entryFrame->second->mEntry);
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

    std::vector<SaleAnteEntry> StateBeforeTxHelper::getAllSaleAntes() {
        std::vector<SaleAnteEntry> saleAntes;
        for (auto &entryPair :  mState) {
            auto const &ledgerEntry = entryPair.second->mEntry;
            if (ledgerEntry.data.type() == LedgerEntryType::SALE_ANTE)
                saleAntes.push_back(ledgerEntry.data.saleAnte());
        }
        return saleAntes;
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

    ReviewableRequestFrame::pointer StateBeforeTxHelper::getReviewableRequest(uint64 requestID) {
        LedgerKey key;
        key.type(LedgerEntryType::REVIEWABLE_REQUEST);
        key.reviewableRequest().requestID = requestID;
        if (mState.find(key) == mState.end())
            return nullptr;
        return std::make_shared<ReviewableRequestFrame>(mState[key]->mEntry);
    }
}
}
