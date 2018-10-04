// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "SaleFrame.h"
#include "database/Database.h"
#include "LedgerDelta.h"
#include "xdrpp/printer.h"
#include "AssetFrame.h"
#include "AssetHelperLegacy.h"

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

void SaleFrame::ensureSaleQuoteAsset(SaleEntry const& oe,
    SaleQuoteAsset const& saleQuoteAsset)
{
    if (saleQuoteAsset.price == 0)
        throw runtime_error("Invalid price");

    if (saleQuoteAsset.quoteAsset == oe.baseAsset)
        throw runtime_error("Invalid quote-base asset pair");
}

bool SaleFrame::quoteAssetCompare(SaleQuoteAsset const& l,
    SaleQuoteAsset const& r)
{
    return l.quoteAsset < r.quoteAsset;
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
        if (!AssetFrame::isAssetCodeValid(oe.baseAsset) || !AssetFrame::isAssetCodeValid(oe.defaultQuoteAsset))
            throw runtime_error("invalid asset code");
        if (oe.baseAsset == oe.defaultQuoteAsset)
            throw runtime_error("base asset cannot be equal to defaultQuoteAsset");
        if (oe.endTime <= oe.startTime)
            throw runtime_error("start time is after end time");
        if (oe.softCap > oe.hardCap)
        {
            throw runtime_error("soft cap exceeds hard cap");
        }
        if (!isValidJson(oe.details))
        {
            throw runtime_error("details is invalid");
        }
        if (oe.currentCapInBase > oe.maxAmountToBeSold)
        {
            throw runtime_error("current cap in base exceeds maxAmountToBeSold");
        }
        if (oe.quoteAssets.empty())
        {
            throw runtime_error("Quote assets is empty");
        }

        for (auto const& saleQuoteAsset : oe.quoteAssets)
        {
            ensureSaleQuoteAsset(oe, saleQuoteAsset);
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

uint64_t SaleFrame::getHardCap() const
{
    return mSale.hardCap;
}

uint64_t SaleFrame::getEndTime() const
{
    return mSale.endTime;
}

uint64_t SaleFrame::getID() const
{
    return mSale.saleID;
}

uint64_t SaleFrame::getPrice(AssetCode const& code)
{
    return getSaleQuoteAsset(code).price;
}

uint64_t SaleFrame::getMaxAmountToBeSold() const
{
    return mSale.maxAmountToBeSold;
}

BalanceID const& SaleFrame::getBaseBalanceID() const
{
    return mSale.baseBalance;
}

void SaleFrame::subCurrentCap(AssetCode const& asset, uint64_t const amount)
{
    auto& saleQuoteAsset = getSaleQuoteAsset(asset);
    if (saleQuoteAsset.currentCap < amount)
    {
        CLOG(ERROR, Logging::ENTRY_LOGGER) << "Unexpected state: tring to substract from current cap amount exceeding it: " << xdr::xdr_to_string(mSale) << " amount: " << amount;
        throw runtime_error("Unexpected state: tring to substract from current cap amount exceeding it");
    }


    saleQuoteAsset.currentCap -= amount;
}

SaleQuoteAsset& SaleFrame::getSaleQuoteAsset(AssetCode const& asset)
{
    for (auto i =0; i < mSale.quoteAssets.size(); i++)
    {
        if (mSale.quoteAssets[i].quoteAsset == asset)
        {
            return mSale.quoteAssets[i];
        }
    }

    throw runtime_error("Failed to find quote asset of the sale for specified asset");
}

AccountID const& SaleFrame::getOwnerID() const
{
    return mSale.ownerID;
}

AssetCode const& SaleFrame::getBaseAsset() const
{
    return mSale.baseAsset;
}

AssetCode const& SaleFrame::getDefaultQuoteAsset() const
{
    return mSale.defaultQuoteAsset;
}

bool SaleFrame::convertToBaseAmount(uint64_t const& price,
    uint64_t const& quoteAssetAmount, uint64_t& result)
{
    return bigDivide(result, quoteAssetAmount, ONE, price, ROUND_UP);
}

SaleFrame::pointer SaleFrame::createNew(uint64_t const& id, AccountID const &ownerID, SaleCreationRequest const& request,
    map<AssetCode, BalanceID> balances, uint64_t maxAmountToBeSold)
{
    try
    {
        LedgerEntry entry;
        entry.data.type(LedgerEntryType::SALE);
        auto& sale = entry.data.sale();
        sale.saleID = id;
        sale.ownerID = ownerID;
        sale.baseAsset = request.baseAsset;
        sale.defaultQuoteAsset = request.defaultQuoteAsset;
        sale.startTime = request.startTime;
        sale.endTime = request.endTime;
        sale.softCap = request.softCap;
        sale.hardCap = request.hardCap;
        sale.details = request.details;
        sale.maxAmountToBeSold = maxAmountToBeSold;
        sale.currentCapInBase = 0;
        sale.quoteAssets.clear();
        for (auto const& quoteAsset : request.quoteAssets)
        {
            SaleQuoteAsset saleQuoteAsset;
            saleQuoteAsset.quoteAsset = quoteAsset.quoteAsset;
            saleQuoteAsset.currentCap = 0;
            saleQuoteAsset.price = quoteAsset.price;
            saleQuoteAsset.quoteBalance = balances[quoteAsset.quoteAsset];
            sale.quoteAssets.push_back(saleQuoteAsset);
        }
        sale.baseBalance = balances[request.baseAsset];

        switch (request.ext.v()) {
            case LedgerVersion::EMPTY_VERSION: {
                break;
            }
            case LedgerVersion::TYPED_SALE: {
                sale.ext.v(LedgerVersion::TYPED_SALE);
                const auto saleType = request.ext.saleTypeExt().typedSale.saleType();
                sale.ext.saleTypeExt().typedSale.saleType(saleType);
                break;
            }
            case LedgerVersion::ALLOW_TO_SPECIFY_REQUIRED_BASE_ASSET_AMOUNT_FOR_HARD_CAP: {
                sale.ext.v(LedgerVersion::TYPED_SALE);
                const auto saleType = request.ext.extV2().saleTypeExt.typedSale.saleType();
                sale.ext.saleTypeExt().typedSale.saleType(saleType);
                break;
            }
            case LedgerVersion::STATABLE_SALES: {
                sale.ext.v(LedgerVersion::STATABLE_SALES);
                const auto saleType = request.ext.extV3().saleTypeExt.typedSale.saleType();
                sale.ext.statableSaleExt().saleTypeExt.typedSale.saleType(saleType);
                sale.ext.statableSaleExt().state = request.ext.extV3().state;
                break;
            }
            default: {
                throw std::runtime_error("Unexpected version of sale creation request");
            }
        }

        return std::make_shared<SaleFrame>(entry);
    } catch (...)
    {
        CLOG(ERROR, Logging::ENTRY_LOGGER) << "Failed to create sale from request";
        throw_with_nested(runtime_error("Failed to create sale from request"));
    }
}

uint64_t SaleFrame::getBaseAmountForCurrentCap(AssetCode const& asset)
{
    auto& quoteAsset = getSaleQuoteAsset(asset);
    uint64_t baseAmount;
    if (!convertToBaseAmount(quoteAsset.price, quoteAsset.currentCap, baseAmount))
    {
        CLOG(ERROR, Logging::ENTRY_LOGGER) << "Unexpected state: failed to conver to base amount current cap: " << xdr::xdr_to_string(mSale);
        throw runtime_error("Unexpected state: failed to conver to base amount current cap");
    }

    return baseAmount;
}

uint64_t SaleFrame::getBaseAmountForCurrentCap()
{
    uint64_t amountToIssue = 0;
    for (auto const& quoteAsset : mSale.quoteAssets)
    {
        const uint64_t baseAmountForCurrentCap = getBaseAmountForCurrentCap(quoteAsset.quoteAsset);
        if (!safeSum(amountToIssue, baseAmountForCurrentCap, amountToIssue))
        {
            CLOG(ERROR, Logging::OPERATION_LOGGER) << "Failed to calculate amount to issue for sale: " << xdr::xdr_to_string(mSale);
            throw std::runtime_error("Failed to calculate amount to issue for sale");
        }
    }

    return amountToIssue;
}

bool SaleFrame::tryLockBaseAsset(uint64_t amount)
{
    if (getSaleType() == SaleType::CROWD_FUNDING)
    {
        return true;
    }
    if (!safeSum(mSale.currentCapInBase, amount, mSale.currentCapInBase))
    {
        return false;
    }

    return mSale.currentCapInBase <= mSale.maxAmountToBeSold;
}

void SaleFrame::unlockBaseAsset(uint64_t amount)
{
    // for crowd funding no need to unlock base asset, as we are not locking it
    if (getSaleType() == SaleType::CROWD_FUNDING)
    {
        return;
    }

    if (mSale.currentCapInBase < amount)
    {
        throw runtime_error("Unexpected state: tring to unlock more then we have in current cap in base asset");
    }

    mSale.currentCapInBase -= amount;
}

void SaleFrame::migrateToVersion(LedgerVersion version)
{
    if (version == mSale.ext.v()) {
        return;
    }

    if (version < mSale.ext.v()) {
        throw std::runtime_error("Trying to migrate sale to lower version");
    }

    auto allVersion = xdr::xdr_traits<LedgerVersion>::enum_values();
    for (auto rawCurentVersion : allVersion) {
        auto currentVersion = LedgerVersion(rawCurentVersion);
        if (mSale.ext.v() >= currentVersion) {
            continue;
        }


        if (currentVersion > version) {
            break;
        }

        switch (currentVersion) {
        case LedgerVersion::TYPED_SALE:
            throw std::runtime_error("Not able to migrate sale from empty version to types sale");
        case LedgerVersion::STATABLE_SALES:
            auto typedSale = mSale.ext.saleTypeExt();
            mSale.ext.v(LedgerVersion::STATABLE_SALES);
            mSale.ext.statableSaleExt().saleTypeExt = typedSale;
            mSale.ext.statableSaleExt().state = SaleState::NONE;
            break;
        }
    }

    if (mSale.ext.v() != version) {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "unexpected state failed to migrate sale to version: " << xdr::xdr_to_string(version);
        throw std::runtime_error("unexpected state: failed to migrate to specified version for sale");
    }
}

void SaleFrame::setSaleState(SaleState state)
{
    setSaleState(mSale, state);
}

SaleType SaleFrame::getSaleType() const
{
    return getSaleType(mSale);
}

SaleType SaleFrame::getSaleType(SaleEntry const& sale)
{
    const auto version = sale.ext.v();
    switch (version)
    {
    case LedgerVersion::EMPTY_VERSION:
        return DEFAULT_SALE_TYPE;
    case LedgerVersion::TYPED_SALE:
        return sale.ext.saleTypeExt().typedSale.saleType();
    case LedgerVersion::STATABLE_SALES:
        return sale.ext.statableSaleExt().saleTypeExt.typedSale.saleType();
    default:
        CLOG(ERROR, Logging::ENTRY_LOGGER) << "Unexpected version of sale entry: " << xdr::xdr_to_string(version);
        throw runtime_error("Unexpected version of sale entry");
    }
}

void SaleFrame::setSaleType(SaleEntry& sale, const SaleType saleType)
{
    const auto version = sale.ext.v();
    switch (version)
    {
    case LedgerVersion::EMPTY_VERSION:
        if (saleType != DEFAULT_SALE_TYPE)
        {
            throw invalid_argument("Trying to set non default sale type to not TYPED_SALE sale");
        }

        return;
    case LedgerVersion::TYPED_SALE:
        if (saleType == DEFAULT_SALE_TYPE)
        {
            throw invalid_argument("Trying to set default sale type to TYPED_SALE sale");
        }

        sale.ext.saleTypeExt().typedSale.saleType(saleType);
        return;
    case LedgerVersion::STATABLE_SALES:
        if (saleType == DEFAULT_SALE_TYPE)
        {
            throw invalid_argument("Trying to set default sale type to STATABLE_SALES sale");
        }

        sale.ext.statableSaleExt().saleTypeExt.typedSale.saleType(saleType);
        return;
    default:
        CLOG(ERROR, Logging::ENTRY_LOGGER) << "Unexpected ledger version of sale. version: " << xdr::xdr_to_string(version);
        throw runtime_error("Unexpected ledger version of sale");

    }
}

void SaleFrame::setSaleState(SaleEntry & sale, SaleState saleState)
{
    switch (sale.ext.v()) {
    case LedgerVersion::STATABLE_SALES:
        sale.ext.statableSaleExt().state = saleState;
        return;
    default:
        if (saleState == SaleState::NONE) {
            return;
        }
        throw std::runtime_error("Unexpected action: not able to set state for sale of unexpected version");
    }
}

void SaleFrame::normalize()
{
    sort(mSale.quoteAssets.begin(), mSale.quoteAssets.end(), &quoteAssetCompare);
}
SaleState SaleFrame::getState()
{
    switch (mSale.ext.v()) {
    case LedgerVersion::STATABLE_SALES:
        return mSale.ext.statableSaleExt().state;
    default:
        return SaleState::NONE;
    }
}

bool SaleFrame::isEndTimeValid(uint64 endTime, uint64 ledgerCloseTime)
{
    return endTime > mSale.startTime && endTime > ledgerCloseTime;
}
}

