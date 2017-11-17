#pragma once

// Copyright 2015 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "overlay/StellarXDR.h"

namespace stellar
{
namespace LedgerTestUtils
{

// note: entries generated are valid in the sense that they are sane by
// themselves
// it does NOT mean that it makes sense relative to other entries

void makeValid(AccountEntry& a);

LedgerEntry generateValidLedgerEntry(size_t b = 3);
std::vector<LedgerEntry> generateValidLedgerEntries(size_t n);

AccountEntry generateValidAccountEntry(size_t b = 3);
std::vector<AccountEntry> generateValidAccountEntries(size_t n);

LedgerEntry makeValid(LedgerEntry& le);

CoinsEmissionRequestEntry generateCoinsEmissionRequestEntry(size_t b = 3);
std::vector<CoinsEmissionRequestEntry> generateCoinsEmissionRequestEntries(size_t n);

CoinsEmissionEntry generateCoinsEmissionEntry(size_t b = 3);
std::vector<CoinsEmissionEntry> generateCoinsEmissionEntries(size_t n);

FeeEntry generateFeeEntry(size_t b);
std::vector<FeeEntry> generateFeeEntries(size_t n);

}
}
