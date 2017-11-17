// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ledger/AccountTypeLimitsFrame.h"
#include "database/Database.h"
#include "crypto/SecretKey.h"
#include "crypto/SHA.h"
#include "LedgerDelta.h"
#include "util/types.h"

using namespace std;
using namespace soci;

namespace stellar
{

const char* AccountTypeLimitsFrame::kSQLCreateStatement1 =
    "CREATE TABLE account_type_limits"
    "("
    "account_type INT    NOT NULL PRIMARY KEY,"
    "daily_out          BIGINT NOT NULL,"
    "weekly_out        BIGINT NOT NULL,"
	"monthly_out        BIGINT NOT NULL,"
    "annual_out        BIGINT NOT NULL,"
    "lastmodified   INT    NOT NULL"
    ");";



AccountTypeLimitsFrame::AccountTypeLimitsFrame() : EntryFrame(ACCOUNT_TYPE_LIMITS), mAccountTypeLimits(mEntry.data.accountTypeLimits())
{
}

AccountTypeLimitsFrame::AccountTypeLimitsFrame(LedgerEntry const& from)
    : EntryFrame(from), mAccountTypeLimits(mEntry.data.accountTypeLimits())
{
}

AccountTypeLimitsFrame::AccountTypeLimitsFrame(AccountTypeLimitsFrame const& from) : AccountTypeLimitsFrame(from.mEntry)
{
}

AccountTypeLimitsFrame& AccountTypeLimitsFrame::operator=(AccountTypeLimitsFrame const& other)
{
    if (&other != this)
    {
        mAccountTypeLimits = other.mAccountTypeLimits;
        mKey = other.mKey;
        mKeyCalculated = other.mKeyCalculated;
    }
    return *this;
}

bool
AccountTypeLimitsFrame::isValid(AccountTypeLimitsEntry const& oe)
{
    auto limits = oe.limits;
    if (limits.dailyOut > limits.weeklyOut)
        return false;
    if (limits.weeklyOut > limits.monthlyOut)
        return false;
    if (limits.monthlyOut > limits.annualOut)
        return false;
    return true;
}

bool
AccountTypeLimitsFrame::isValid() const
{
    return isValid(mAccountTypeLimits);
}


uint64_t
AccountTypeLimitsFrame::countObjects(soci::session& sess)
{
    uint64_t count = 0;
    sess << "SELECT COUNT(*) FROM account_type_limits;", into(count);
    return count;
}

void
AccountTypeLimitsFrame::storeDelete(LedgerDelta& delta, Database& db) const
{
    storeDelete(delta, db, getKey());
}

void
AccountTypeLimitsFrame::storeDelete(LedgerDelta& delta, Database& db, LedgerKey const& key)
{
    return;
}

void
AccountTypeLimitsFrame::storeChange(LedgerDelta& delta, Database& db)
{
    storeUpdateHelper(delta, db, false);
}

void
AccountTypeLimitsFrame::storeAdd(LedgerDelta& delta, Database& db)
{
    storeUpdateHelper(delta, db, true);
}

void
AccountTypeLimitsFrame::storeUpdateHelper(LedgerDelta& delta, Database& db, bool insert)
{
    touch(delta);
    flushCachedEntry(db);
        
    if (!isValid())
    {
        throw std::runtime_error("Invalid AccountTypeLimits state");
    }
    
    string sql;
        
    if (insert)
    {
        sql = "INSERT INTO account_type_limits (account_type, daily_out, weekly_out, monthly_out, annual_out, lastmodified) VALUES (:at, :do, :wo, :mo, :ao, :lm)";
    }
    else
    {
        sql = "UPDATE account_type_limits SET daily_out=:do, weekly_out = :wo, monthly_out = :mo, annual_out = :ao, lastmodified=:lm WHERE account_type = :at";
    }
    
    auto prep = db.getPreparedStatement(sql);
    auto& st = prep.statement();
    int accountType = mAccountTypeLimits.accountType;
    auto limits = mAccountTypeLimits.limits;
    st.exchange(use(accountType, "at"));
    st.exchange(use(limits.dailyOut, "do"));
    st.exchange(use(limits.weeklyOut, "wo"));
    st.exchange(use(limits.monthlyOut, "mo"));
    st.exchange(use(limits.annualOut, "ao"));
    st.exchange(use(getLastModified(), "lm"));
    st.define_and_bind();
    
    auto timer =
    insert ? db.getInsertTimer("AccountTypeLimits") : db.getUpdateTimer("AccountTypeLimits");
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

AccountTypeLimitsFrame::pointer AccountTypeLimitsFrame::loadLimits(AccountType accountType, Database& db, LedgerDelta* delta)
    {
		LedgerKey key;
		key.type(ACCOUNT_TYPE_LIMITS);
		key.accountTypeLimits().accountType = accountType;
		if (cachedEntryExists(key, db))
		{
			auto p = getCachedEntry(key, db);
			return p ? make_shared<AccountTypeLimitsFrame>(*p) : nullptr;
		}

		LedgerEntry le;
		le.data.type(ACCOUNT_TYPE_LIMITS);
		le.data.accountTypeLimits().accountType = accountType;
        
		auto prep =
			db.getPreparedStatement("SELECT daily_out, weekly_out, monthly_out, annual_out, lastmodified "
                "FROM account_type_limits where account_type = :at");

		int rawAccountType = int(accountType);
		auto& st = prep.statement();
		st.exchange(into(le.data.accountTypeLimits().limits.dailyOut));
		st.exchange(into(le.data.accountTypeLimits().limits.weeklyOut));
        st.exchange(into(le.data.accountTypeLimits().limits.monthlyOut));
        st.exchange(into(le.data.accountTypeLimits().limits.annualOut));
		st.exchange(into(le.lastModifiedLedgerSeq));
		st.exchange(use(rawAccountType));
		st.define_and_bind();
		{
			auto timer = db.getSelectTimer("AccountTypeLimits-state");
			st.execute(true);
		}

		if (!st.got_data())
		{
			putCachedEntry(key, nullptr, db);
			return nullptr;
		}

		auto res = make_shared<AccountTypeLimitsFrame>(le);
		assert(res->isValid());
		res->mKeyCalculated = false;
		res->putCachedEntry(db);
		if (delta)
		{
			delta->recordEntry(*res);
		}
		return res;
    }



bool
AccountTypeLimitsFrame::exists(Database& db, LedgerKey const& key)
{
    return false;
}

void
AccountTypeLimitsFrame::dropAll(Database& db)
{
    db.getSession() << "DROP TABLE IF EXISTS account_type_limits;";
    db.getSession() << kSQLCreateStatement1;
}
}
