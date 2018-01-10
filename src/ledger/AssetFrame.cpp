// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "AssetFrame.h"
#include "database/Database.h"
#include "LedgerDelta.h"
#include "ledger/LedgerManager.h"
#include "util/types.h"
#include <util/basen.h>
#include "util/format.h"
#include "xdrpp/printer.h"
#include <locale>

using namespace soci;
using namespace std;

namespace stellar
{
using xdr::operator<;

AssetFrame::AssetFrame() : EntryFrame(LedgerEntryType::ASSET)
                         , mAsset(mEntry.data.asset())
{
}

AssetFrame::AssetFrame(LedgerEntry const& from)
    : EntryFrame(from)
    , mAsset(mEntry.data.asset())
{
}

AssetFrame::AssetFrame(AssetFrame const& from) : AssetFrame(from.mEntry)
{
}

AssetFrame& AssetFrame::operator=(AssetFrame const& other)
{
    if (&other != this)
    {
        mAsset = other.mAsset;
        mKey = other.mKey;
        mKeyCalculated = other.mKeyCalculated;
    }
    return *this;
}

AssetFrame::pointer AssetFrame::create(AssetCreationRequest const& request,
                                       AccountID const& owner)
{
    LedgerEntry le;
    le.data.type(LedgerEntryType::ASSET);
    AssetEntry& asset = le.data.asset();
    asset.availableForIssueance = request.initialPreissuedAmount;
    asset.code = request.code;
    asset.details = request.details;
    asset.issued = 0;
    asset.maxIssuanceAmount = request.maxIssuanceAmount;
    asset.owner = owner;
    asset.policies = request.policies;
    asset.preissuedAssetSigner = request.preissuedAssetSigner;
    asset.pendingIssuance = 0;
    return std::make_shared<AssetFrame>(le);
}

uint64_t AssetFrame::getPendingIssuance() const
{
    return mAsset.pendingIssuance;
}

bool AssetFrame::willExceedMaxIssuanceAmount(const uint64_t amount) const
{
    uint64_t issued;
    if (!safeSum(issued, {mAsset.issued, amount, mAsset.pendingIssuance}))
    {
        return true;
    }

    return issued > mAsset.maxIssuanceAmount;
}

bool AssetFrame::tryIssue(uint64_t amount)
{
    if (willExceedMaxIssuanceAmount(amount))
    {
        return false;
    }

    if (!isAvailableForIssuanceAmountSufficient(amount))
    {
        return false;
    }

    mAsset.availableForIssueance -= amount;
    mAsset.issued += amount;
    return true;
}

bool AssetFrame::canAddAvailableForIssuance(uint64_t amount)
{
    uint64_t availableForIssuance;
    if (!safeSum(mAsset.availableForIssueance, amount, availableForIssuance))
        return false;

    uint64_t maxAmountCanBeIssuedAfterUpdate;
    if (!safeSum(mAsset.issued, availableForIssuance,
                 maxAmountCanBeIssuedAfterUpdate))
        return false;

    return maxAmountCanBeIssuedAfterUpdate < mAsset.maxIssuanceAmount;
}

bool AssetFrame::tryAddAvailableForIssuance(uint64_t amount)
{
    if (!canAddAvailableForIssuance(amount))
        return false;

    mAsset.availableForIssueance += amount;
    return true;
}

bool AssetFrame::tryWithdraw(const uint64_t amount)
{
    if (mAsset.issued < amount)
    {
        return false;
    }

    mAsset.issued -= amount;
    return true;
}

bool AssetFrame::lockIssuedAmount(const uint64_t amount)
{
    if (willExceedMaxIssuanceAmount(amount))
    {
        return false;
    }

    if (!isAvailableForIssuanceAmountSufficient(amount))
    {
        return false;
    }

    mAsset.availableForIssueance -= amount;
    mAsset.pendingIssuance += amount;
    return true;
}

void AssetFrame::mustUnlockIssuedAmount(uint64_t const amount)
{
    if (mAsset.pendingIssuance < amount)
    {
        CLOG(ERROR, Logging::ENTRY_LOGGER) << "Expected amount to be less then pending issuance; asset: " << xdr::xdr_to_string(mAsset) << "; amount: " << amount;
        throw runtime_error("Expected amount to be less then pending issuance");
    }

    mAsset.pendingIssuance -= amount;
    if (!safeSum(amount, mAsset.availableForIssueance, mAsset.availableForIssueance))
    {
        CLOG(ERROR, Logging::ENTRY_LOGGER) << "Overflow on unlock issued amount. Failed to add availableForIssueance; asset: "
            << xdr::xdr_to_string(mAsset) << "; amount: " << amount;
        throw runtime_error("Overflow on unlock issued amount. Failed to add availableForIssueance");
    }
}

bool AssetFrame::isAssetCodeValid(AssetCode const& code)
{
    bool zeros = false;
    bool onechar = false; // at least one non zero character
    for (uint8_t b : code)
    {
        if (b == 0)
        {
            zeros = true;
        }
        else if (zeros)
        {
            // zeros can only be trailing
            return false;
        }
        else
        {
            if (b > 0x7F || !std::isalnum((char)b, cLocale))
            {
                return false;
            }
            onechar = true;
        }
    }
    return onechar;
}

void AssetFrame::ensureValid(AssetEntry const& oe)
{
    try
    {
        uint64_t totalIssuedOrLocked;
        if (!safeSum(totalIssuedOrLocked, { oe.issued, oe.pendingIssuance }))
        {
            throw runtime_error("Overflow during calculation of totalIssuedOrLocked");
        }

        if (oe.maxIssuanceAmount < totalIssuedOrLocked)
        {
            throw runtime_error("totalIssuedOrLocked exceeds max issuance amount");
        }

       if (!isAssetCodeValid(oe.code))
       {
           throw runtime_error("asset code is invalid");
       }
    }
    catch (...)
    {
        CLOG(ERROR, Logging::ENTRY_LOGGER) << "Unexpected asset entry is invalid: " << xdr::xdr_to_string(oe);
        throw_with_nested(runtime_error("Asset entry is invalid"));
    }
}

void AssetFrame::ensureValid() const
{
    ensureValid(mAsset);
}
}
