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
        
    static bool isValid(SaleEntry const& oe);
    bool isValid() const;

    SaleEntry& getSaleEntry();

    static bool calculateRequiredBaseAssetForSoftCap(SaleCreationRequest const& request, uint64_t& requiredAmount);

    static pointer createNew(uint64_t const& id, AccountID const &ownerID, SaleCreationRequest& request);

};
}
