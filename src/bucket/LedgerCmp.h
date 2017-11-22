#pragma once

// Copyright 2015 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "overlay/StellarXDR.h"
#include "ledger/EntryFrame.h"
#include "crypto/Hex.h"

namespace stellar
{
using xdr::operator<;

/**
 * Compare two LedgerEntries or LedgerKeys for 'identity', not content.
 *
 * LedgerEntries are identified iff they have:
 *
 *   - The same type
 *     - If accounts, then with same accountID
 *     - If trustlines, then with same (accountID, asset) pair
 *     - If offers, then with same (sellerID, sequence) pair
 *
 * Equivalently: Two LedgerEntries have the same 'identity' iff their
 * corresponding LedgerKeys are exactly equal. This operator _could_ be
 * implemented in terms of extracting 2 LedgerKeys from 2 LedgerEntries and
 * doing operator< on them, but that would be comparatively inefficient.
 */
struct LedgerEntryIdCmp
{
    template <typename T, typename U>
    auto operator()(T const& a, U const& b) const -> decltype(a.type(),
                                                              b.type(), bool())
    {
        LedgerEntryType aty = a.type();
        LedgerEntryType bty = b.type();

        if (aty < bty)
            return true;

        if (aty > bty)
            return false;

        switch (aty)
        {

        case ACCOUNT:
            return a.account().accountID < b.account().accountID;

		case COINS_EMISSION_REQUEST:
		{
			auto const& ar = a.coinsEmissionRequest();
			auto const& br = b.coinsEmissionRequest();
			if (ar.issuer < br.issuer)
				return true;
			if (br.issuer < ar.issuer)
				return false;
			return ar.requestID < br.requestID;
		}
        case FEE:
        {
            auto const& af = a.feeState();
            auto const& bf = b.feeState();
            auto hashAStr = binToHex(af.hash);
            auto hashBStr = binToHex(bf.hash);
			if (hashAStr < hashBStr)
				return true;
			if (hashBStr < hashAStr)
				return false;
			if (af.lowerBound < bf.lowerBound)
				return true;
			if (bf.lowerBound < af.lowerBound)
				return false;
			return af.upperBound < bf.upperBound;
        }
		case COINS_EMISSION:
		{
			auto const& ac = a.coinsEmission();
			auto const& bc = b.coinsEmission();
			return ac.serialNumber < bc.serialNumber;
		}
		case BALANCE:
		{
			auto const& ab = a.balance();
			auto const& bb = b.balance();
			return ab.balanceID < bb.balanceID;
		}
		case PAYMENT_REQUEST:
		{
			auto const& ab = a.paymentRequest();
			auto const& bb = b.paymentRequest();
			return ab.paymentID < bb.paymentID;
		}
		case ASSET:
		{
			auto const& aa = a.asset();
			auto const& ba = b.asset();
			return aa.code < ba.code;
        }
        case ACCOUNT_TYPE_LIMITS:
        {
            auto const& aatl = a.accountTypeLimits();
			auto const& batl = b.accountTypeLimits();
            return aatl.accountType < batl.accountType;
        }
        case STATISTICS:
        {
            auto const& as = a.stats();
			auto const& bs = b.stats();
            return as.accountID < bs.accountID;
        }
        case REFERENCE_ENTRY:
        {
            auto const& ap = a.payment();
			auto const& bp = b.payment();
			return ap.reference < bp.reference;
        }
		case TRUST:
		{
			auto const& at = a.trust();
			auto const& bt = b.trust();
            if (at.balanceToUse < bt.balanceToUse)
				return true;
			if (bt.balanceToUse < at.balanceToUse)
				return false;
			return at.allowedAccount < bt.allowedAccount;
		}
        case ACCOUNT_LIMITS:
        {
            auto const& al = a.accountLimits();
			auto const& bl = b.accountLimits();
            return al.accountID < bl.accountID;
        }
case ASSET_PAIR:
		{
			auto const& ap = a.assetPair();
			auto const& bp = b.assetPair();
			if (ap.base < bp.base)
				return true;
			if (bp.base < ap.base)
				return false;
			return ap.quote < bp.quote;
		}
		case OFFER_ENTRY:
		{
			auto const& ap = a.offer();
			auto const& bp = b.offer();
			if (ap.offerID < bp.offerID)
				return true;
			if (bp.offerID < ap.offerID)
				return false;
			return ap.ownerID < bp.ownerID;
		}
		case INVOICE:
		{
			auto const& ai = a.invoice();
			auto const& bi = b.invoice();
			return ai.invoiceID < bi.invoiceID;
		}
		case REVIEWABLE_REQUEST:
		{
			auto const& ar = a.reviewableRequest();
			auto const& br = b.reviewableRequest();
			return ar.ID < br.ID;
		}
        }

        return false;
    }

    template <typename T>
    bool operator()(T const& a, LedgerEntry const& b) const
    {
        return (*this)(a, b.data);
    }

    template <typename T, typename = typename std::enable_if<
                              !std::is_same<T, LedgerEntry>::value>::type>
    bool operator()(LedgerEntry const& a, T const& b) const
    {
        return (*this)(a.data, b);
    }
};

/**
 * Compare two BucketEntries for identity by comparing their respective
 * LedgerEntries (ignoring their hashes, as the LedgerEntryIdCmp ignores their
 * bodies).
 */
struct BucketEntryIdCmp
{
    LedgerEntryIdCmp mCmp;
    bool operator()(BucketEntry const& a, BucketEntry const& b) const
    {
        BucketEntryType aty = a.type();
        BucketEntryType bty = b.type();

        if (aty == LIVEENTRY)
        {
            if (bty == LIVEENTRY)
            {
                return mCmp(a.liveEntry(), b.liveEntry());
            }
            else
            {
                return mCmp(a.liveEntry(), b.deadEntry());
            }
        }
        else
        {
            if (bty == LIVEENTRY)
            {
                return mCmp(a.deadEntry(), b.liveEntry());
            }
            else
            {
                return mCmp(a.deadEntry(), b.deadEntry());
            }
        }
    }
};
}
