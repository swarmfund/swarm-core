#pragma once

// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ledger/EntryFrame.h"
#include "ledger/LedgerManager.h"
#include <functional>

namespace soci
{
class session;
}

namespace stellar
{
class StatementContext;

class SaleFrame : public EntryFrame
{

    SaleEntry& mSale;

    SaleFrame(SaleFrame const& from);


    static void ensureSaleQuoteAsset(SaleEntry const& oe, SaleQuoteAsset const& saleQuoteAsset);

    static bool quoteAssetCompare(SaleQuoteAsset const& l, SaleQuoteAsset const& r);

    static const SaleType DEFAULT_SALE_TYPE = SaleType(0);

  public:
    enum class State : int32_t
    {
        ACTIVE = 1,
        NOT_STARTED_YET = 2,
        ENDED = 3
    };


    typedef std::shared_ptr<SaleFrame> pointer;

    SaleFrame();
    SaleFrame(LedgerEntry const& from);

    SaleFrame& operator=(SaleFrame const& other);

    EntryFrame::pointer
    copy() const override
    {
        return EntryFrame::pointer(new SaleFrame(*this));
    }
    
    // ensureValid - throws exception if entry is not valid
    static void ensureValid(SaleEntry const& oe);
    void ensureValid() const;

    SaleEntry& getSaleEntry();

    uint64_t getStartTime() const;
    uint64_t getSoftCap() const;
    uint64_t getHardCap() const;
    uint64_t getEndTime() const;
    uint64_t getID() const;
    uint64_t getPrice(AssetCode const& code);
    uint64_t getMaxAmountToBeSold() const;
    BalanceID const& getBaseBalanceID() const;
    void subCurrentCap(AssetCode const& asset, uint64_t const amount);

    SaleQuoteAsset& getSaleQuoteAsset(AssetCode const& asset);

    AccountID const& getOwnerID() const;

    AssetCode const& getBaseAsset() const;
    AssetCode const& getDefaultQuoteAsset() const;

    static bool convertToBaseAmount(uint64_t const& price, uint64_t const& quoteAssetAmount, uint64_t& result);

    static pointer createNew(uint64_t const& id, AccountID const &ownerID, SaleCreationRequest const& request,
        std::map<AssetCode, BalanceID> balances, uint64_t maxAmountToBeSold);

    uint64_t getBaseAmountForCurrentCap(AssetCode const& asset);
    uint64_t getBaseAmountForCurrentCap();


    bool tryLockBaseAsset(uint64_t amount);
    void unlockBaseAsset(uint64_t amount);

    SaleType getSaleType() const;
    static SaleType getSaleType(SaleEntry const& sale);

    static void setSaleType(SaleEntry& sale, SaleType saleType);

    void normalize();

};
}
