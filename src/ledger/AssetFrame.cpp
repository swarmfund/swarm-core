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

AssetFrame::AssetFrame() : EntryFrame(LedgerEntryType::ASSET), mAsset(mEntry.data.asset())
{
}

AssetFrame::AssetFrame(LedgerEntry const& from)
	: EntryFrame(from), mAsset(mEntry.data.asset())
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

AssetFrame::pointer AssetFrame::create(AssetCreationRequest const & request, AccountID const& owner)
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
        asset.lockedIssuance = 0;
	return std::make_shared<AssetFrame>(le);
}

bool AssetFrame::willExceedMaxIssuanceAmount(uint64_t amount) {
	uint64_t issued;
	if (!safeSum(issued, { mAsset.issued, amount, mAsset.lockedIssuance})) {
		return true;
	}

	return issued > mAsset.maxIssuanceAmount;
}

bool AssetFrame::tryIssue(uint64_t amount) {
	if (willExceedMaxIssuanceAmount(amount)) {
		return false;
	}

	if (!isAvailableForIssuanceAmountSufficient(amount)) {
		return false;
	}

	mAsset.availableForIssueance -= amount;
	mAsset.issued += amount;
	return true;
}

bool AssetFrame::canAddAvailableForIssuance(uint64_t amount) {
	uint64_t availableForIssuance;
	if (!safeSum(mAsset.availableForIssueance, amount, availableForIssuance))
		return false;

	uint64_t maxAmountCanBeIssuedAfterUpdate;
	if (!safeSum(mAsset.issued, availableForIssuance, maxAmountCanBeIssuedAfterUpdate))
		return false;

	return maxAmountCanBeIssuedAfterUpdate < mAsset.maxIssuanceAmount;
}

bool AssetFrame::tryAddAvailableForIssuance(uint64_t amount) {
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
    uint64_t currentlyLockedForIssuance;
    if (!safeSum(currentlyLockedForIssuance, {mAsset.issued, mAsset.lockedIssuance}))
    {
        CLOG(ERROR, Logging::ENTRY_LOGGER) << "Overflow on sum of issued and lockedIssuance " << mAsset.code;
        throw runtime_error("Overflow on sum of issued and lockedIssuance");
    }

    uint64_t updatedLockedForIssuance;
    if (!safeSum(currentlyLockedForIssuance, amount, updatedLockedForIssuance))
    {
        return false;
    }

    mAsset.lockedIssuance = updatedLockedForIssuance;
    return mAsset.maxIssuanceAmount <= updatedLockedForIssuance;

}

bool AssetFrame::isAssetCodeValid(AssetCode const & code)
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

bool
AssetFrame::isValid(AssetEntry const& oe)
{
	uint64_t canBeIssued;
	if (!safeSum(canBeIssued, { oe.issued, oe.availableForIssueance})) {
		return false;
	}

        if (oe.maxIssuanceAmount < canBeIssued)
            return false;

        uint64_t totalIssuedOrLocked;
    if (!safeSum(totalIssuedOrLocked, {oe.issued, oe.lockedIssuance}))
    {
        return false;
    }

    if (oe.maxIssuanceAmount < totalIssuedOrLocked)
        return false;

	return isAssetCodeValid(oe.code);
}

bool
AssetFrame::isValid() const
{
	return isValid(mAsset);
}

}