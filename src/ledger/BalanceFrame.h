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
    static void
    loadBalances(StatementContext& prep,
               std::function<void(LedgerEntry const&)> BalanceProcessor);

    BalanceEntry& mBalance;

    BalanceFrame(BalanceFrame const& from);

    void storeUpdateHelper(LedgerDelta& delta, Database& db, bool insert);

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

    int64_t
    getAmount()
    {
        return mBalance.amount;
    }

    int64_t
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

    // Instance-based overrides of EntryFrame.
    void storeDelete(LedgerDelta& delta, Database& db) const override;
    void storeChange(LedgerDelta& delta, Database& db) override;
    void storeAdd(LedgerDelta& delta, Database& db) override;

    // Static helpers that don't assume an instance.
    static void storeDelete(LedgerDelta& delta, Database& db,
                            LedgerKey const& key);
	static bool exists(Database& db, LedgerKey const& key);
	static bool exists(Database& db, BalanceID balanceID);
    static uint64_t countObjects(soci::session& sess);

    // database utilities
	// tries to load balance, throw exception if fails
	static pointer mustLoadBalance(BalanceID balanceID, Database& db, LedgerDelta* delta = nullptr) {
		auto result = loadBalance(balanceID, db, delta);
		if (!!result) {
			return result;
		}

		CLOG(ERROR, Logging::ENTRY_LOGGER) << "expected balance " << BalanceKeyUtils::toStrKey(balanceID) << " to exist";
		throw std::runtime_error("expected balance to exist");
	}

    static pointer loadBalance(BalanceID balanceID,
                             Database& db, LedgerDelta* delta = nullptr);

    static pointer loadBalance(AccountID account, AssetCode asset, Database& db,
                      LedgerDelta* delta);

    static void loadBalances(AccountID const& accountID,
                           std::vector<BalanceFrame::pointer>& retBalances,
                           Database& db);

    static std::unordered_map<std::string, BalanceFrame::pointer>
    loadBalances(AccountID const& accountID, Database& db);

    // load all Balances from the database (very slow)
    static std::unordered_map<AccountID, std::vector<BalanceFrame::pointer>>
    loadAllBalances(Database& db);

    static std::vector<BalanceFrame::pointer>
    loadBalancesRequiringDemurrage(AssetCode asset, uint64 currentPeriodStart,
                       Database& db);

	static pointer createNew(BalanceID id, AccountID owner, AssetCode asset,
        uint64 ledgerCloseTime, uint64 initialAmount = 0);

    static void dropAll(Database& db);
    static const char* kSQLCreateStatement1;

};
}
