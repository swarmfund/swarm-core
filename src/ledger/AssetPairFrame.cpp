// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "AssetPairFrame.h"
#include "AssetFrame.h"
#include "crypto/SecretKey.h"
#include "crypto/Hex.h"
#include "database/Database.h"
#include "LedgerDelta.h"
#include "ledger/LedgerManager.h"
#include "util/basen.h"
#include "util/types.h"
#include "lib/util/format.h"
#include <algorithm>

using namespace soci;
using namespace std;

namespace stellar
{
using xdr::operator<;

static const char* assetPairColumnSelector =
"SELECT base, quote, current_price, physical_price, physical_price_correction, max_price_step, policies, "
		"lastmodified, version "
"FROM asset_pair";

AssetPairFrame::AssetPairFrame() : EntryFrame(LedgerEntryType::ASSET_PAIR), mAssetPair(mEntry.data.assetPair())
{
}

AssetPairFrame::AssetPairFrame(LedgerEntry const& from)
    : EntryFrame(from), mAssetPair(mEntry.data.assetPair())
{
}

AssetPairFrame::AssetPairFrame(AssetPairFrame const& from) : AssetPairFrame(from.mEntry)
{
}

AssetPairFrame& AssetPairFrame::operator=(AssetPairFrame const& other)
{
    if (&other != this)
    {
        mAssetPair = other.mAssetPair;
        mKey = other.mKey;
        mKeyCalculated = other.mKeyCalculated;
    }
    return *this;
}

AssetPairFrame::pointer AssetPairFrame::create(AssetCode base, AssetCode quote, int64_t currentPrice, int64_t physicalPrice,
	int64_t physicalPriceCorrection, int64_t maxPriceStep, int32_t policies)
{
	LedgerEntry le;
	le.data.type(LedgerEntryType::ASSET_PAIR);
	AssetPairEntry& assetPair = le.data.assetPair();
	assetPair.base = base;
	assetPair.quote = quote;
	assetPair.currentPrice = currentPrice;
	assetPair.physicalPrice = physicalPrice;
	assetPair.physicalPriceCorrection = physicalPriceCorrection;
	assetPair.maxPriceStep = maxPriceStep;
	assetPair.policies = policies;
	return make_shared<AssetPairFrame>(le);
}

bool
AssetPairFrame::isValid(AssetPairEntry const& oe)
{
	return AssetFrame::isAssetCodeValid(oe.base) && AssetFrame::isAssetCodeValid(oe.quote) && oe.currentPrice >= 0 && oe.maxPriceStep >= 0
		&& oe.physicalPrice >= 0 && oe.physicalPriceCorrection >= 0;
}

bool
AssetPairFrame::isValid() const
{
    return isValid(mAssetPair);
}

bool AssetPairFrame::getPhysicalPriceWithCorrection(int64_t& result) const
{
	return bigDivide(result, mAssetPair.physicalPrice, mAssetPair.physicalPriceCorrection, 100 * ONE, ROUND_UP);
}

int64_t AssetPairFrame::getMinPriceInTermsOfCurrent() const
{
	int64_t minPriceInTermsOfCurrent = 0;
	if (checkPolicy(AssetPairPolicy::CURRENT_PRICE_RESTRICTION))
	{
		int64_t maxPrice = 0;
		if (!getCurrentPriceCoridor(minPriceInTermsOfCurrent, maxPrice))
		{
			throw std::runtime_error("Current price coridor overflow");
		}
	}

	return minPriceInTermsOfCurrent;
}

int64_t AssetPairFrame::getMinPriceInTermsOfPhysical() const
{
	int64_t minPriceInTermsOfPhysical = 0;
	if (checkPolicy(AssetPairPolicy::PHYSICAL_PRICE_RESTRICTION))
	{
		if (!getPhysicalPriceWithCorrection(minPriceInTermsOfPhysical))
		{
			throw std::runtime_error("Physical price calculation overflow");
		}
	}

	return minPriceInTermsOfPhysical;
}

int64_t AssetPairFrame::getMinAllowedPrice() const
{
	return  getMinPriceInTermsOfPhysical();
}

bool AssetPairFrame::getCurrentPriceCoridor(int64_t& min, int64_t& max) const
{
	int64_t minInPercent = 100 * ONE - mAssetPair.maxPriceStep;
	if (minInPercent < 0)
		return false;
	int64_t maxInPercent = 100 * ONE + mAssetPair.maxPriceStep;
	if (maxInPercent < 0)
		return false;
	return bigDivide(min, mAssetPair.currentPrice, minInPercent, 100 * ONE, ROUND_UP) 
		&& bigDivide(max, mAssetPair.currentPrice, maxInPercent, 100 * ONE, ROUND_DOWN);
}

}