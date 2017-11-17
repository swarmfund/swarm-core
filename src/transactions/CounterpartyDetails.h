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

struct CounterpartyDetails
{
	AccountFrame::pointer mAccount;
	const std::vector<AccountType> mAllowedAccountTypes;
	const bool mIsBlockedAllowed;
	const bool mIsMustExists;

	CounterpartyDetails(std::vector<AccountType> allowedAccountTypes, bool isBlockedAllowed, bool mustExists) :
		mAllowedAccountTypes(allowedAccountTypes), 
		mIsBlockedAllowed(isBlockedAllowed),
		mIsMustExists(mustExists)
	{
	}
};
}
