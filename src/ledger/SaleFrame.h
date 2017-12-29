#pragma once

// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ledger/EntryFrame.h"
#include "ledger/LedgerManager.h"
#include <functional>
#include <unordered_map>

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

  public:
    typedef std::shared_ptr<SaleFrame> pointer;

    SaleFrame();
    SaleFrame(LedgerEntry const& from);

    SaleFrame& operator=(SaleFrame const& other);

    EntryFrame::pointer
    copy() const override
    {
        return EntryFrame::pointer(new SaleFrame(*this));
    }
    
    // ensureValid - throws exeption if entry is not valid
    static void ensureValid(SaleEntry const& oe);
    void ensureValid() const;

    SaleEntry& getSaleEntry();

    uint64_t getStartTime() const;
    uint64_t getSoftCap() const;
    uint64_t getCurrentCap() const;
    uint64_t getEndTime() const;
    uint64_t getPrice() const;
    uint64_t getID() const;
    uint64_t getBaseAmountForCurrentCap() const;
    BalanceID const& getBaseBalanceID() const;
    BalanceID const& getQuoteBalanceID() const;

    AccountID const& getOwnerID() const;

    AssetCode const& getBaseAsset() const;

    AssetCode const& getQuoteAsset() const;

    // tryAddCap - returns false, if additional amount will exceed hard cap
    bool tryAddCap(uint64_t amount);
    void subCurrentCap(uint64_t amount);

    static bool convertToBaseAmount(uint64_t const& price, uint64_t const& quoteAssetAmount, uint64_t& result);

    static pointer createNew(uint64_t const& id, AccountID const &ownerID, SaleCreationRequest const& request,
        BalanceID const& baseBalance, BalanceID const& quoteBalance);

};
}
