// Copyright 2015 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "LedgerTestUtils.h"
#include "ledger/AccountFrame.h"
#include "crypto/SecretKey.h"
#include "crypto/SHA.h"
#include "util/types.h"
#include <string>
#include <cctype>
#include <xdrpp/autocheck.h>

namespace stellar
{
namespace LedgerTestUtils
{

template <typename T>
void
clampLow(T low, T& v)
{
    if (v < low)
    {
        v = low;
    }
}

template <typename T>
T
mustPositive(T raw)
{
	return raw < 0 ? -raw : raw;
}



template <typename T>
void
clampHigh(T high, T& v)
{
    if (v > high)
    {
        v = high;
    }
}

template <typename T> void
stripControlCharacters(T& s)
{
    std::locale loc("C");

    for (auto it = s.begin(); it != s.end();)
    {
        char c = static_cast<char>(*it);
        if (c < 0 || std::iscntrl(c))
        {
            it = s.erase(it);
        }
        else
        {
            it++;
        }
    }
}

static bool
signerEqual(Signer const& s1, Signer const& s2)
{
    return s1.pubKey == s2.pubKey;
}

void
makeValid(AccountEntry& a)
{
	a.accountID = SecretKey::random().getPublicKey();
    std::sort(a.signers.begin(), a.signers.end(), &AccountFrame::signerCompare);
    a.signers.erase(
        std::unique(a.signers.begin(), a.signers.end(), signerEqual),
        a.signers.end());
    for (auto& s : a.signers)
    {
        if (s.weight == 0)
        {
            s.weight = 100;
        }
    }
}

void
makeValid(FeeEntry& o)
{
	o.fixedFee = mustPositive<int64_t>(o.fixedFee);
	o.percentFee = mustPositive<int64_t>(o.percentFee);
	clampHigh(100 * ONE, o.percentFee);
	o.lowerBound = mustPositive<int64_t>(o.lowerBound);
	o.upperBound = mustPositive<int64_t>(o.upperBound);
	clampHigh(o.upperBound, o.lowerBound);
	auto allFees = getAllFeeTypes();
	o.feeType = allFees[rand() % allFees.size()];	
}

void
makeValid(BalanceEntry& o)
{

	clampLow<uint64_t>(1, o.amount);
	clampLow<uint64_t>(1, o.locked);
}

void
makeValid(PaymentRequestEntry& o)
{
	if (o.sourceSend < 0)
	{
		o.sourceSend = -o.sourceSend;
	}

	if (o.sourceSendUniversal < 0)
	{
		o.sourceSendUniversal = -o.sourceSendUniversal;
	}

	if (o.destinationReceive < 0)
	{
		o.destinationReceive = -o.destinationReceive;
	}

	clampLow<int64_t>(1, o.sourceSend);
	clampLow<int64_t>(1, o.sourceSendUniversal);
	clampLow<int64_t>(1, o.destinationReceive);
}

void
makeValid(ReferenceEntry& o)
{

}

std::string randomAssetCode() {
	return SecretKey::random().getStrKeyPublic().substr(0, 15);;
}

void
makeValid(AssetEntry& o)
{
	o.code = randomAssetCode();
	stripControlCharacters(o.code);
}

void
makeValid(AssetPairEntry& o)
{
	o.base = randomAssetCode();
	stripControlCharacters(o.base);
	o.quote = randomAssetCode();
	stripControlCharacters(o.quote);
	clampLow<int64_t>(1, o.physicalPrice);
	clampLow<int64_t>(o.physicalPrice, o.currentPrice);
	o.maxPriceStep = o.maxPriceStep < 0 ? -o.maxPriceStep : 0;
	o.physicalPriceCorrection = o.physicalPriceCorrection < 0 ? -o.physicalPriceCorrection : 0;
	if (o.policies < 0) {
		o.policies = -o.policies;
	}
}


void
makeValid(TrustEntry& o)
{
	
}

void makeValid(Limits& o) {
	if (o.dailyOut < 0)
	{
		o.dailyOut = -o.dailyOut;
	}

	clampLow<int64_t>(1, o.dailyOut);
	clampLow<int64_t>(o.dailyOut, o.weeklyOut);
	clampLow<int64_t>(o.weeklyOut, o.monthlyOut);
	clampLow<int64_t>(o.monthlyOut, o.annualOut);
}

void
makeValid(AccountTypeLimitsEntry& o)
{
	o.accountType = AccountType(rand());
	makeValid(o.limits);
}


void
makeValid(StatisticsEntry& o)
{
	clampLow<uint64_t>(1, o.dailyOutcome);
	clampLow<uint64_t>(o.dailyOutcome, o.weeklyOutcome);
	clampLow<uint64_t>(o.weeklyOutcome, o.monthlyOutcome);
	clampLow<uint64_t>(o.monthlyOutcome, o.annualOutcome);
}

void
makeValid(OfferEntry& o)
{
	stripControlCharacters(o.base);
	stripControlCharacters(o.quote);
	clampLow<int64_t>(1, o.baseAmount);
	clampLow<int64_t>(1, o.quoteAmount);
	clampLow<int64_t>(1, o.fee);
	clampLow<int64_t>(1, o.percentFee);
	clampLow<int64_t>(1, o.price);
}

void
makeValid(AccountLimitsEntry& o)
{
	makeValid(o.limits);
}

LedgerEntry makeValid(LedgerEntry& le)
{
	auto& led = le.data;
	switch (led.type())
	{
	case LedgerEntryType::ACCOUNT:
		makeValid(led.account());
		break;
    case LedgerEntryType::FEE:
		makeValid(led.feeState());
		break;
    case LedgerEntryType::BALANCE:
		makeValid(led.balance());
		break;
	case LedgerEntryType::PAYMENT_REQUEST:
		makeValid(led.paymentRequest());
		break;
	case LedgerEntryType::ASSET:
		makeValid(led.asset());
		break;
	case LedgerEntryType::REFERENCE_ENTRY:
		makeValid(led.reference());
		break;
	case LedgerEntryType::ACCOUNT_TYPE_LIMITS:
		makeValid(led.accountTypeLimits());
		break;
	case LedgerEntryType::STATISTICS:
		makeValid(led.stats());
		break;
	case LedgerEntryType::TRUST:
		makeValid(led.trust());
		break;
	case LedgerEntryType::ACCOUNT_LIMITS:
		makeValid(led.accountLimits());
		break;
	case LedgerEntryType::ASSET_PAIR:
		makeValid(led.assetPair());
		break;
	case LedgerEntryType::OFFER_ENTRY:
		makeValid(led.offer());
		break;
	default:
		throw std::runtime_error("Unexpected entry type");
	}

	return le;
}

static auto validLedgerEntryGenerator = autocheck::map(
    [](LedgerEntry&& le, size_t s)
    {
		return makeValid(le);
    },
    autocheck::generator<LedgerEntry>());

static auto validAccountEntryGenerator = autocheck::map(
    [](AccountEntry&& ae, size_t s)
    {
        makeValid(ae);
        return ae;
    },
    autocheck::generator<AccountEntry>());

static auto validFeeEntryGenerator = autocheck::map(
	[](FeeEntry&& o, size_t s)
{
	makeValid(o);
	return o;
},
autocheck::generator<FeeEntry>());

LedgerEntry
generateValidLedgerEntry(size_t b)
{
    return validLedgerEntryGenerator(b);
}

std::vector<LedgerEntry>
generateValidLedgerEntries(size_t n)
{
    static auto vecgen = autocheck::list_of(validLedgerEntryGenerator);
    return vecgen(n);
}

AccountEntry
generateValidAccountEntry(size_t b)
{
    return validAccountEntryGenerator(b);
}

std::vector<AccountEntry>
generateValidAccountEntries(size_t n)
{
    static auto vecgen = autocheck::list_of(validAccountEntryGenerator);
    return vecgen(n);
}

FeeEntry generateFeeEntry(size_t b)
{
	return validFeeEntryGenerator(b);
}
std::vector<FeeEntry> generateFeeEntries(size_t n)
{
	static auto vecgen = autocheck::list_of(validFeeEntryGenerator);
	return vecgen(n);
}

}
}
