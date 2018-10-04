#pragma once

// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ledger/EntryFrame.h"
#include "ledger/LedgerManager.h"
#include "ledger/FeeFrame.h"
#include <functional>
#include <unordered_map>

namespace soci
{
class session;
}

namespace stellar
{
class StatementContext;

class BalanceFrame : public EntryFrame
{
    BalanceEntry& mBalance;

    BalanceFrame(BalanceFrame const& from);

  public:
    typedef std::shared_ptr<BalanceFrame> pointer;

    BalanceFrame();
    BalanceFrame(LedgerEntry const& from);

    BalanceFrame& operator=(BalanceFrame const& other);

    EntryFrame::pointer
    copy() const override
    {
        return EntryFrame::pointer(new BalanceFrame(*this));
    }

    BalanceEntry const&
    getBalance() const
    {
        return mBalance;
    }

    uint64_t
    getAmount()
    {
        return mBalance.amount;
    }

    uint64_t
    getLocked()
    {
        return mBalance.locked;
    }

    AssetCode
    getAsset()
    {
        return mBalance.asset;
    }

    AccountID
    getAccountID()
    {
        return mBalance.accountID;
    }

    BalanceID
    getBalanceID()
    {
        return mBalance.balanceID;
    }

    uint64_t getTotal() const;
        
    static bool isValid(BalanceEntry const& oe);
    bool isValid() const;
	[[deprecated]]
	bool addBalance(int64_t delta);
	[[deprecated]]
    bool addLocked(int64_t delta);
    
    enum Result {SUCCESS, LINE_FULL, UNDERFUNDED};
    
	[[deprecated]]
	Result lockBalance(int64_t delta);
	// returns false if total amount of funds (balance amount + locked) exceeds UINT64_MAX
	bool tryFundAccount(uint64_t amount);
        // tryLock - changes from balance and add to locked
        Result tryLock(uint64_t amountToBeLocked);
        // chargeFromLocked - Charges specified amount from locked. Returns false, if failed to do it.
        bool tryChargeFromLocked(uint64_t amountToCharge);
        // unlock - removes from lock and adds to balance specified amount. Returns false - if fails
        bool unlock(uint64_t amountToUnlock);
        // tryCharge - charges specified amount from available amount. Returns false, if failed to do it;
        bool tryCharge(uint64_t amountToCharge);

	static pointer createNew(BalanceID id, AccountID owner, AssetCode asset);

};
}
