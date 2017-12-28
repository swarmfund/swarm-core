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

void
SaleFrame::ensureValid(SaleEntry const& oe)
{
    try
    {
        if (!AssetFrame::isAssetCodeValid(oe.baseAsset) || !AssetFrame::isAssetCodeValid(oe.quoteAsset))
            throw runtime_error("invalid asset code");
        if (oe.baseAsset == oe.quoteAsset)
            throw runtime_error("base asset can not be equeal quote");
        if (oe.endTime <= oe.startTime)
            throw runtime_error("start time is after end time");
        if (oe.softCap > oe.hardCap)
        {
            throw runtime_error("soft cap exceeds hard cap");
        }
    } catch (...)
    {
        CLOG(ERROR, Logging::ENTRY_LOGGER) << "Unexpected state sale entry is invalid: " << xdr::xdr_to_string(oe);
        throw_with_nested(runtime_error("Sale entry is invalid"));
    }
}

void
SaleFrame::ensureValid() const
{
    ensureValid(mSale);
}

SaleEntry& SaleFrame::getSaleEntry()
{
    return mSale;
}

uint64_t SaleFrame::getStartTime() const
{
    return mSale.startTime;
}


uint64_t SaleFrame::getSoftCap() const
{
    return mSale.softCap;
}

uint64_t SaleFrame::getCurrentCap() const
{
    return mSale.currentCap;
}

uint64_t SaleFrame::getEndTime() const
{
    return mSale.endTime;
}

uint64_t SaleFrame::getPrice() const
{
    return mSale.price;
}

uint64_t SaleFrame::getID() const
{
    return mSale.saleID;
}

uint64_t SaleFrame::getBaseAmountForCurrentCap() const
{
    uint64_t baseAmount;
    if (!convertToBaseAmount(mSale.price, mSale.currentCap, baseAmount))
    {
        CLOG(ERROR, Logging::ENTRY_LOGGER) << "Unexpected state: failed to conver to base amount current cap: " << xdr::xdr_to_string(mSale);
        throw runtime_error("Unexpected state: failed to conver to base amount current cap");
    }

    return baseAmount;
}

BalanceID const& SaleFrame::getBaseBalanceID() const
{
    return mSale.baseBalance;
}

BalanceID const& SaleFrame::getQuoteBalanceID() const
{
    return mSale.quoteBalance;
}

AccountID const& SaleFrame::getOwnerID() const
{
    return mSale.ownerID;
}

AssetCode const& SaleFrame::getBaseAsset() const
{
    return mSale.baseAsset;
}

AssetCode const& SaleFrame::getQuoteAsset() const
{
    return mSale.quoteAsset;
}

bool SaleFrame::tryAddCap(const uint64_t amount)
{
    uint64_t updatedCap;
    if (!safeSum(mSale.currentCap, amount, updatedCap))
    {
        return false;
    }

    if (mSale.hardCap < updatedCap)
    {
        return false;
    }

    mSale.hardCap = updatedCap;
    return true;
}

void SaleFrame::subCurrentCap(const uint64_t amount)
{
    if (mSale.currentCap < amount)
    {
        CLOG(ERROR, Logging::ENTRY_LOGGER) << "Unexpected state: tring to substract from current cap amount exceeding it: " << xdr::xdr_to_string(mSale) << " amount: " << amount;
        throw runtime_error("Unexpected state: tring to substract from current cap amount exceeding it");
    }

    mSale.currentCap -= amount;
}

bool SaleFrame::convertToBaseAmount(uint64_t const& price,
    uint64_t const& quoteAssetAmount, uint64_t& result)
{
    return bigDivide(result, quoteAssetAmount, ONE, price, ROUND_UP);
}

SaleFrame::pointer SaleFrame::createNew(uint64_t const& id, AccountID const &ownerID, SaleCreationRequest const& request,
    BalanceID const& baseBalance, BalanceID const& quoteBalance)
{
    LedgerEntry entry;
    entry.data.type(LedgerEntryType::SALE);
    auto& sale = entry.data.sale();
    sale.saleID = id;
    sale.ownerID = ownerID;
    sale.baseAsset = request.baseAsset;
    sale.quoteAsset = request.quoteAsset;
    sale.startTime = request.startTime;
    sale.endTime = request.endTime;
    sale.price = request.price;
    sale.softCap = request.softCap;
    sale.hardCap = request.hardCap;
    sale.details = request.details;
    sale.currentCap = 0;
    sale.baseBalance = baseBalance;
    sale.quoteBalance = quoteBalance;

    return std::make_shared<SaleFrame>(entry);
}
}

