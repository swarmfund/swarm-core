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
    static void
    loadAssetPairs(StatementContext& prep,
               std::function<void(LedgerEntry const&)> AssetPairProcessor);

    AssetPairEntry& mAssetPair;

    AssetPairFrame(AssetPairFrame const& from);

    void storeUpdateHelper(LedgerDelta& delta, Database& db, bool insert);

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
		return (mAssetPair.policies & policy) == policy;
	}

    static bool isValid(AssetPairEntry const& oe);
    bool isValid() const;

    // Instance-based overrides of EntryFrame.
    void storeDelete(LedgerDelta& delta, Database& db) const override;
    void storeChange(LedgerDelta& delta, Database& db) override;
    void storeAdd(LedgerDelta& delta, Database& db) override;

    // Static helpers that don't assume an instance.
    static void storeDelete(LedgerDelta& delta, Database& db,
                            LedgerKey const& key);
	static bool exists(Database& db, LedgerKey const& key);
	static bool exists(Database& db, AssetCode base, AssetCode quote);
    static uint64_t countObjects(soci::session& sess);

    // database utilities
    static pointer loadAssetPair(AssetCode base, AssetCode quote,
                             Database& db, LedgerDelta* delta = nullptr);
	static pointer mustLoadAssetPair(AssetCode base, AssetCode quote,
		Database& db, LedgerDelta* delta = nullptr)
	{
		auto result = loadAssetPair(base, quote, db, delta);
		if (!result)
		{
			CLOG(ERROR, Logging::ENTRY_LOGGER) << "Unexpected db state. Expected asset pair to exists. Base " << base << " Quote " << quote;
			throw std::runtime_error("Unexpected db state. Expected asset pair to exist");
		}

		return result;
	}

	static void loadAssetPairsByQuote(AssetCode quoteAsset, Database& db, std::vector<AssetPairFrame::pointer>& retAssetPairs);

	static pointer create(AssetCode base, AssetCode quote, int64_t currentPrice, int64_t physicalPrice, int64_t physicalPriceCorrection, int64_t maxPriceStep, int32_t policies);


    static void dropAll(Database& db);
    static const char* kSQLCreateStatement1;
};
}
