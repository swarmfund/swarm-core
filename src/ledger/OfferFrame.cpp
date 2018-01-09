// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ledger/OfferFrame.h"
#include "LedgerDelta.h"
#include "crypto/SHA.h"
#include "crypto/SecretKey.h"
#include "database/Database.h"
#include "util/types.h"
#include "AssetFrame.h"
#include "xdrpp/printer.h"

using namespace std;
using namespace soci;

namespace stellar
{

	OfferFrame::OfferFrame() : EntryFrame(LedgerEntryType::OFFER_ENTRY), mOffer(mEntry.data.offer())
	{
	}

	OfferFrame::OfferFrame(LedgerEntry const& from)
		: EntryFrame(from), mOffer(mEntry.data.offer())
	{
	}

	OfferFrame::OfferFrame(OfferFrame const& from) : OfferFrame(from.mEntry)
	{
	}

	OfferFrame&
		OfferFrame::operator=(OfferFrame const& other)
	{
		if (&other != this)
		{
			mOffer = other.mOffer;
			mKey = other.mKey;
			mKeyCalculated = other.mKeyCalculated;
		}
		return *this;
	}

	int64_t OfferFrame::getPrice() const
	{
		return mOffer.price;
	}

	uint64
		OfferFrame::getOfferID() const
	{
		return mOffer.offerID;
	}

uint64_t OfferFrame::getOrderBookID() const
{
    return mOffer.orderBookID;
}

bool
		OfferFrame::isValid(OfferEntry const& oe)
	{
		return AssetFrame::isAssetCodeValid(oe.base)
			   && AssetFrame::isAssetCodeValid(oe.quote)
			   && oe.baseAmount > 0
			   && oe.quoteAmount > 0
			   && oe.price > 0
			   && oe.fee >= 0;
	}

	bool
		OfferFrame::isValid() const
	{
		return isValid(mOffer);
	}

	EntryFrame::pointer OfferFrame::copy() const {
		return EntryFrame::pointer(new OfferFrame(*this));
	}

	OfferEntry const &OfferFrame::getOffer() const {
		return mOffer;
	}

	OfferEntry &OfferFrame::getOffer() {
		return mOffer;
	}

uint64_t OfferFrame::getLockedAmount() const
{
    if (!mOffer.isBuy)
    {
        return mOffer.baseAmount;
    }
    
    uint64_t result;
    if (!safeSum(mOffer.quoteAmount, mOffer.fee, result))
    {
        CLOG(ERROR, Logging::ENTRY_LOGGER) << "Overflow during locked amount calculation: " << xdr::xdr_to_string(mOffer);
        throw runtime_error("Overflow on locked amount canculation");
    }

    return result;
}

BalanceID const& OfferFrame::getLockedBalance() const
{
    return mOffer.isBuy ? mOffer.quoteBalance : mOffer.baseBalance;
}
}
