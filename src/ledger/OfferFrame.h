#pragma once

// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ledger/EntryFrame.h"
#include <functional>
#include <unordered_map>

namespace soci
{
	class session;
}

namespace stellar
{
	class ManageOfferOpFrame;
	class StatementContext;

	class OfferFrame : public EntryFrame
	{
		static void
			loadOffers(StatementContext& prep,
				std::function<void(LedgerEntry const&)> offerProcessor);

		OfferEntry& mOffer;

		OfferFrame(OfferFrame const& from);

		void storeUpdateHelper(LedgerDelta& delta, Database& db, bool insert);

	public:
		typedef std::shared_ptr<OfferFrame> pointer;

		OfferFrame();
		OfferFrame(LedgerEntry const& from);

		OfferFrame& operator=(OfferFrame const& other);

		EntryFrame::pointer
			copy() const override
		{
			return EntryFrame::pointer(new OfferFrame(*this));
		}

		int64_t getPrice() const;
		uint64 getOfferID() const;

		OfferEntry const&
			getOffer() const
		{
			return mOffer;
		}
		OfferEntry&
			getOffer()
		{
			return mOffer;
		}

		static bool isValid(OfferEntry const& oe);
		bool isValid() const;

		// Instance-based overrides of EntryFrame.
		void storeDelete(LedgerDelta& delta, Database& db) const override;
		void storeChange(LedgerDelta& delta, Database& db) override;
		void storeAdd(LedgerDelta& delta, Database& db) override;

		// Static helpers that don't assume an instance.
		static void storeDelete(LedgerDelta& delta, Database& db,
			LedgerKey const& key);
		static bool exists(Database& db, LedgerKey const& key);
		static uint64_t countObjects(soci::session& sess);

		// database utilities
		static pointer loadOffer(AccountID const& accountID, uint64_t offerID,
			Database& db, LedgerDelta* delta = nullptr);

		static void loadBestOffers(size_t numOffers, size_t offset,
			AssetCode const& base, AssetCode const& quote,
			bool isBuy,
			std::vector<OfferFrame::pointer>& retOffers,
			Database& db);

		// load all offers from the database (very slow)
		static std::unordered_map<AccountID, std::vector<OfferFrame::pointer>>
			loadAllOffers(Database& db);

		static void loadOffersWithPriceLower(AssetCode const& base, AssetCode const& quote,
			int64_t price, std::vector<OfferFrame::pointer>& retOffers, Database& db);

		static void dropAll(Database& db);
		static const char* kSQLCreateStatement1;
		static const char* kSQLCreateStatement2;
	};
}
