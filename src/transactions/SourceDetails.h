#pragma once

// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "util/types.h"
#include "ledger/AccountFrame.h"

namespace stellar
{
class Application;
class LedgerManager;
class LedgerDelta;

struct SourceDetails
{
	const std::vector<AccountType> mAllowedSourceAccountTypes;
	const int32_t mNeeededTheshold;
	const int32_t mNeededSignedClass;
	const uint32_t mAllowedBlockedReasons;

	SourceDetails(std::vector<AccountType> allowedSourceAccountTypes, int32_t neeededTheshold, int32_t neededSignedClass, uint32_t allowedBlockedReasons = 0) :
		mAllowedSourceAccountTypes(allowedSourceAccountTypes),
		mNeeededTheshold(neeededTheshold),
		mNeededSignedClass(neededSignedClass),
		mAllowedBlockedReasons(allowedBlockedReasons)
	{
	}
};
}
