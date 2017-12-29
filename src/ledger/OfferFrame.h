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
		OfferEntry& mOffer;

		OfferFrame(OfferFrame const& from);

	public:
		typedef std::shared_ptr<OfferFrame> pointer;

		OfferFrame();
		OfferFrame(LedgerEntry const& from);

		OfferFrame& operator=(OfferFrame const& other);

		EntryFrame::pointer copy() const override;

		int64_t getPrice() const;
		uint64 getOfferID() const;

		OfferEntry& getOffer();

                uint64_t getLockedAmount() const;
                BalanceID const& getLockedBalance() const;

		OfferEntry const& getOffer() const;

		bool isValid() const;
		static bool isValid(OfferEntry const& oe);
	};
}
