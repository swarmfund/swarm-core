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

class AssetPairFrame : public EntryFrame
{
    AssetPairEntry& mAssetPair;

    AssetPairFrame(AssetPairFrame const& from);

	bool getPhysicalPriceWithCorrection(int64_t& result) const;
	bool getCurrentPriceCoridor(int64_t& min, int64_t& max) const;

  public:
    typedef std::shared_ptr<AssetPairFrame> pointer;

    AssetPairFrame();
    AssetPairFrame(LedgerEntry const& from);

    AssetPairFrame& operator=(AssetPairFrame const& other);

    EntryFrame::pointer
    copy() const override
    {
        return EntryFrame::pointer(new AssetPairFrame(*this));
    }

    AssetPairEntry const&
    getAssetPair() const
    {
        return mAssetPair;
    }
    AssetPairEntry&
    getAssetPair()
    {
        return mAssetPair;
    }

	AssetCode getBaseAsset() {
		return mAssetPair.base;
	}

	AssetCode getQuoteAsset() {
		return mAssetPair.quote;
	}

	int64_t getCurrentPrice()
	{
		return mAssetPair.currentPrice;
	}

	void setCurrentPrice(int64_t currentPrice)
	{
		mAssetPair.currentPrice = currentPrice;
	}

	int64_t getMinAllowedPrice() const;
	int64_t getMinPriceInTermsOfCurrent() const;
	int64_t getMinPriceInTermsOfPhysical() const;

	bool checkPolicy(AssetPairPolicy policy) const
	{
		auto policyValue = static_cast<int32_t >(policy);
		return (mAssetPair.policies & policyValue) == policyValue;
	}

        // convertAmount - converts amount to quote if code is base or to base if quote. Returns false, if overflow
        bool convertAmount(AssetCode code, uint64_t amount, Rounding rounding, uint64_t& result) const;

    static bool isValid(AssetPairEntry const& oe);
    bool isValid() const;

	static pointer create(AssetCode base, AssetCode quote, int64_t currentPrice, int64_t physicalPrice, int64_t physicalPriceCorrection, int64_t maxPriceStep, int32_t policies);
};
}
