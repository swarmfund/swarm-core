// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "BalanceFrame.h"
#include "crypto/SecretKey.h"
#include "crypto/Hex.h"
#include "database/Database.h"
#include "LedgerDelta.h"
#include "ledger/LedgerManager.h"
#include "util/basen.h"
#include "util/types.h"
#include "lib/util/format.h"
#include <algorithm>
#include "AssetFrame.h"

using namespace soci;
using namespace std;

namespace stellar
{
using xdr::operator<;

const char* BalanceFrame::kSQLCreateStatement1 =
    "CREATE TABLE balance"
    "("
    "balance_id               VARCHAR(56) NOT NULL,"
	"account_id               VARCHAR(56) NOT NULL,"
    "asset                    VARCHAR(16)  NOT NULL,"
    "amount                   BIGINT      NOT NULL CHECK (amount >= 0),"
    "locked                   BIGINT      NOT NULL CHECK (locked >= 0),"
    "lastmodified             INT         NOT NULL, "
    "PRIMARY KEY (balance_id)"
    ");";
static const char* balanceColumnSelector =
"SELECT balance_id, asset, amount, locked, account_id, lastmodified "
"FROM balance";

BalanceFrame::BalanceFrame() : EntryFrame(BALANCE), mBalance(mEntry.data.balance())
{
}

BalanceFrame::BalanceFrame(LedgerEntry const& from)
    : EntryFrame(from), mBalance(mEntry.data.balance())
{
}

BalanceFrame::BalanceFrame(BalanceFrame const& from) : BalanceFrame(from.mEntry)
{
}

BalanceFrame& BalanceFrame::operator=(BalanceFrame const& other)
{
    if (&other != this)
    {
        mBalance = other.mBalance;
        mKey = other.mKey;
        mKeyCalculated = other.mKeyCalculated;
    }
    return *this;
}

BalanceFrame::pointer BalanceFrame::createNew(BalanceID id, AccountID owner, AssetCode asset, uint64 ledgerCloseTime,
    uint64 initialAmount)
{
	LedgerEntry le;
	le.data.type(BALANCE);
	BalanceEntry& entry = le.data.balance();

	entry.balanceID = id;
	entry.accountID = owner;
	entry.asset = asset;
	entry.amount = initialAmount;
	entry.locked = 0;
	return std::make_shared<BalanceFrame>(le);
}

bool
BalanceFrame::isValid(BalanceEntry const& oe)
{
    return AssetFrame::isAssetCodeValid(oe.asset) && oe.locked >= 0 && oe.amount >= 0;
}

bool
BalanceFrame::isValid() const
{
    return isValid(mBalance);
}


bool BalanceFrame::addBalance(int64_t delta)
{
	int64_t availableBalance = getAmount();
	if (availableBalance + delta < 0)
		return false;
	if (mBalance.amount + delta < 0)
		return false;
	mBalance.amount += delta;
    int64_t totalFunds = mBalance.amount + mBalance.locked;
    if (totalFunds < 0)
        return false;
	return true;
}

bool BalanceFrame::addLocked(int64_t delta)
{
	if (mBalance.locked + delta < 0)
		return false;
	mBalance.locked += delta;
    int64_t totalFunds = mBalance.amount + mBalance.locked;
    if (totalFunds < 0)
        return false;
	return true;
}

BalanceFrame::Result BalanceFrame::lockBalance(int64_t delta)
{
	int64_t availableBalance = getAmount();
	if (availableBalance - delta < 0)
		return Result::UNDERFUNDED;
    if (mBalance.locked + delta < 0)
		return Result::LINE_FULL;
	mBalance.amount -= delta;
    mBalance.locked += delta;
	return Result::SUCCESS;
}

bool BalanceFrame::tryFundAccount(uint64_t amount)
{
	uint64_t updatedAmount;
	if (!safeSum(mBalance.amount, amount, updatedAmount)) {
		return false;
	}

	uint64_t totalFunds;
	if (!safeSum(updatedAmount, mBalance.locked, totalFunds)) {
		return false;
	}

	mBalance.amount = updatedAmount;
	return true;
}

BalanceFrame::pointer
BalanceFrame::loadBalance(BalanceID balanceID, Database& db,
                      LedgerDelta* delta)
{
    BalanceFrame::pointer retBalance;
    auto balIDStrKey = BalanceKeyUtils::toStrKey(balanceID);

    std::string sql = balanceColumnSelector;
    sql += " WHERE balance_id = :id";
    auto prep = db.getPreparedStatement(sql);
    auto& st = prep.statement();
    st.exchange(use(balIDStrKey));

    auto timer = db.getSelectTimer("balance");
    loadBalances(prep, [&retBalance](LedgerEntry const& Balance)
               {
                   retBalance = make_shared<BalanceFrame>(Balance);
               });

    if (delta && retBalance)
    {
        delta->recordEntry(*retBalance);
    }

    return retBalance;
}

BalanceFrame::pointer
BalanceFrame::loadBalance(AccountID account, AssetCode asset, Database& db,
                      LedgerDelta* delta)
{
    BalanceFrame::pointer retBalance;
    string actIDStrKey;
    string assetCode = asset;

    actIDStrKey = PubKeyUtils::toStrKey(account);
    std::string sql = balanceColumnSelector;
    sql += " WHERE account_id = :aid AND asset = :as";
    auto prep = db.getPreparedStatement(sql);
    auto& st = prep.statement();
    st.exchange(use(actIDStrKey));
    st.exchange(use(assetCode));

    auto timer = db.getSelectTimer("balance");
    loadBalances(prep, [&retBalance](LedgerEntry const& Balance)
               {
                   retBalance = make_shared<BalanceFrame>(Balance);
               });

    if (delta && retBalance)
    {
        delta->recordEntry(*retBalance);
    }

    return retBalance;
}


void
BalanceFrame::loadBalances(StatementContext& prep,
                       std::function<void(LedgerEntry const&)> balanceProcessor)
{
    string accountID, balanceID, asset;

    LedgerEntry le;
    le.data.type(BALANCE);
    BalanceEntry& oe = le.data.balance();

	// SELECT balance_id, asset, amount, locked, account_id, lastmodified 

    statement& st = prep.statement();
    st.exchange(into(balanceID));
    st.exchange(into(asset));
    st.exchange(into(oe.amount));
    st.exchange(into(oe.locked));
    st.exchange(into(accountID));
    st.exchange(into(le.lastModifiedLedgerSeq));
    st.define_and_bind();
    st.execute(true);
    while (st.got_data())
    {
        oe.accountID = PubKeyUtils::fromStrKey(accountID);
        oe.balanceID = BalanceKeyUtils::fromStrKey(balanceID);
        oe.asset = asset;
        
        if (!isValid(oe))
        {
            throw std::runtime_error("Invalid Recovery request");
        }

        balanceProcessor(le);
        st.fetch();
    }
}

void
BalanceFrame::loadBalances(AccountID const& accountID,
                       std::vector<BalanceFrame::pointer>& retBalances,
                       Database& db)
{
    std::string actIDStrKey;
    actIDStrKey = PubKeyUtils::toStrKey(accountID);

    std::string sql = balanceColumnSelector;
    sql += " WHERE account_id = :account_id";
    auto prep = db.getPreparedStatement(sql);
    auto& st = prep.statement();
    st.exchange(use(actIDStrKey));

    auto timer = db.getSelectTimer("balance");
    loadBalances(prep, [&retBalances](LedgerEntry const& of)
               {
                   retBalances.emplace_back(make_shared<BalanceFrame>(of));
               });
}

std::unordered_map<string, BalanceFrame::pointer>
BalanceFrame::loadBalances(AccountID const& accountID, Database& db)
{
    std::unordered_map<string, BalanceFrame::pointer> retBalances;
    std::string actIDStrKey, rawAsset;
    actIDStrKey = PubKeyUtils::toStrKey(accountID);

    std::string sql = balanceColumnSelector;
    sql += " WHERE account_id = :account_id";
    auto prep = db.getPreparedStatement(sql);
    auto& st = prep.statement();
    st.exchange(use(actIDStrKey));

    auto timer = db.getSelectTimer("balance");
    loadBalances(prep, [&retBalances](LedgerEntry const& of)
               {
                   retBalances[of.data.balance().asset] = make_shared<BalanceFrame>(of);
               });
    return retBalances;
}


std::unordered_map<AccountID, std::vector<BalanceFrame::pointer>>
BalanceFrame::loadAllBalances(Database& db)
{
    std::unordered_map<AccountID, std::vector<BalanceFrame::pointer>> retBalances;
    std::string sql = balanceColumnSelector;
    sql += " ORDER BY account_id";
    auto prep = db.getPreparedStatement(sql);
    
    auto timer = db.getSelectTimer("balance");
    loadBalances(prep, [&retBalances](LedgerEntry const& of)
                                {
                                    auto& thisUserBalance = retBalances[of.data.balance().accountID];
                                    thisUserBalance.emplace_back(make_shared<BalanceFrame>(of));
                                });
    return retBalances;
}

std::vector<BalanceFrame::pointer>
BalanceFrame::loadBalancesRequiringDemurrage(AssetCode asset, uint64 currentPeriodStart, Database& db)
{
    std::string sql = balanceColumnSelector;
    std::vector<BalanceFrame::pointer> retBalances;
    std::string rawAsset = asset;
    sql += " WHERE asset = :asset AND ORDER BY balance_id DESC LIMIT 50";
    auto prep = db.getPreparedStatement(sql);
    auto& st = prep.statement();
    st.exchange(use(rawAsset));
    st.exchange(use(currentPeriodStart));

    auto timer = db.getSelectTimer("balance");
    loadBalances(prep, [&retBalances](LedgerEntry const& of)
               {
                   retBalances.emplace_back(make_shared<BalanceFrame>(of));
               });
    return retBalances;
}



uint64_t
BalanceFrame::countObjects(soci::session& sess)
{
    uint64_t count = 0;
    sess << "SELECT COUNT(*) FROM balance;", into(count);
    return count;
}

void
BalanceFrame::storeDelete(LedgerDelta& delta, Database& db) const
{
    storeDelete(delta, db, getKey());
}

void
BalanceFrame::storeDelete(LedgerDelta& delta, Database& db, LedgerKey const& key)
{
    auto timer = db.getDeleteTimer("balance");
    auto prep = db.getPreparedStatement("DELETE FROM balance WHERE balance_id=:id");
    auto& st = prep.statement();
    auto balanceID = key.balance().balanceID;
    auto balIDStrKey = BalanceKeyUtils::toStrKey(balanceID);
    st.exchange(use(balIDStrKey));
    st.define_and_bind();
    st.execute(true);
    delta.deleteEntry(key);
}

void
BalanceFrame::storeChange(LedgerDelta& delta, Database& db)
{
    storeUpdateHelper(delta, db, false);
}

void
BalanceFrame::storeAdd(LedgerDelta& delta, Database& db)
{
    storeUpdateHelper(delta, db, true);
}

void
BalanceFrame::storeUpdateHelper(LedgerDelta& delta, Database& db, bool insert)
{
    touch(delta);

    if (!isValid())
    {
        throw std::runtime_error("Invalid balance");
    }

	std::string accountID = PubKeyUtils::toStrKey(mBalance.accountID);
    std::string balanceID = BalanceKeyUtils::toStrKey(mBalance.balanceID);
    std::string asset = mBalance.asset;

    string sql;

    if (insert)
    {
		sql = "INSERT INTO balance (balance_id, asset,"
			"amount, locked, account_id, lastmodified)"
			"VALUES (:id, :as, :am, :ld, :aid, :lm)";
    }
    else
    {
        sql = "UPDATE balance SET "
			"asset = :as, amount=:am, locked=:ld, account_id=:aid, lastmodified=:lm "
              "WHERE balance_id = :id";
    }

    auto prep = db.getPreparedStatement(sql);
    auto& st = prep.statement();


    st.exchange(use(balanceID, "id"));
    st.exchange(use(asset, "as"));
    st.exchange(use(mBalance.amount, "am"));
    st.exchange(use(mBalance.locked, "ld"));
    st.exchange(use(accountID, "aid"));
	st.exchange(use(mEntry.lastModifiedLedgerSeq, "lm"));
    st.define_and_bind();

    auto timer =
        insert ? db.getInsertTimer("balance") : db.getUpdateTimer("balance");
    st.execute(true);

    if (st.get_affected_rows() != 1)
    {
        throw std::runtime_error("could not update SQL");
    }

    if (insert)
    {
        delta.addEntry(*this);
    }
    else
    {
        delta.modEntry(*this);
    }

}

bool
BalanceFrame::exists(Database& db, LedgerKey const& key)
{
	int exists = 0;
	auto timer = db.getSelectTimer("balance-exists");
	auto prep =
		db.getPreparedStatement("SELECT EXISTS (SELECT NULL FROM balance WHERE balance_id=:id)");
	auto& st = prep.statement();
    auto balanceID = key.balance().balanceID;
    auto balIDStrKey = BalanceKeyUtils::toStrKey(balanceID);
	st.exchange(use(balIDStrKey));
	st.exchange(into(exists));
	st.define_and_bind();
	st.execute(true);

	return exists != 0;
}

bool
BalanceFrame::exists(Database& db, BalanceID balanceID)
{
	int exists = 0;
	auto timer = db.getSelectTimer("balance-exists");
	auto prep =
		db.getPreparedStatement("SELECT EXISTS (SELECT NULL FROM balance WHERE balance_id=:id)");
    auto balIDStrKey = BalanceKeyUtils::toStrKey(balanceID);
	auto& st = prep.statement();
	st.exchange(use(balIDStrKey));
	st.exchange(into(exists));
	st.define_and_bind();
	st.execute(true);

	return exists != 0;
}

void
BalanceFrame::dropAll(Database& db)
{
    db.getSession() << "DROP TABLE IF EXISTS balance;";
    db.getSession() << kSQLCreateStatement1;
}
}

