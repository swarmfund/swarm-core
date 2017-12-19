// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "SaleFrame.h"
#include "database/Database.h"
#include "LedgerDelta.h"
#include "xdrpp/printer.h"
#include "AssetFrame.h"

using namespace soci;
using namespace std;

namespace stellar
{
using xdr::operator<;

SaleFrame::SaleFrame() : EntryFrame(LedgerEntryType::REVIEWABLE_REQUEST), mSale(mEntry.data.sale())
{
}

SaleFrame::SaleFrame(LedgerEntry const& from)
    : EntryFrame(from), mSale(mEntry.data.sale())
{
}

SaleFrame::SaleFrame(SaleFrame const& from) : SaleFrame(from.mEntry)
{
}

SaleFrame& SaleFrame::operator=(SaleFrame const& other)
{
    if (&other != this)
    {
        mSale = other.mSale;
        mKey = other.mKey;
        mKeyCalculated = other.mKeyCalculated;
    }
    return *this;
}

bool
SaleFrame::isValid(SaleEntry const& oe)
{
    if (!AssetFrame::isAssetCodeValid(oe.baseAsset) || AssetFrame::isAssetCodeValid(oe.quoteAsset))
        return false;
    if (oe.baseAsset == oe.quoteAsset)
        return false;
    if (oe.endTime <= oe.startTime)
        return false;
    return oe.softCap < oe.hardCap;
}

bool
SaleFrame::isValid() const
{
    return isValid(mSale);
}

SaleEntry& SaleFrame::getSaleEntry()
{
    return mSale;
}

bool SaleFrame::calculateRequiredBaseAssetForSoftCap(
    SaleCreationRequest const& request, uint64_t& requiredAmount)
{
    return bigDivide(requiredAmount, request.softCap, ONE, request.price, ROUND_UP);
}

SaleFrame::pointer SaleFrame::createNew(uint64_t const& id, AccountID const &ownerID, SaleCreationRequest& request)
{
    LedgerEntry entry;
    entry.data.type(LedgerEntryType::SALE);
    auto& sale = entry.data.sale();
    sale.saleID = id;
    sale.ownerID = ownerID;
    sale.baseAsset = request.baseAsset;
    sale.quoteAsset = request.quoteAsset;
    sale.name = request.name;
    sale.startTime = request.startTime;
    sale.endTime = request.endTime;
    sale.price = request.price;
    sale.softCap = request.softCap;
    sale.hardCap = request.hardCap;
    sale.details = request.details;
    sale.currentCap = 0;

    return std::make_shared<SaleFrame>(entry);
}
}

