// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ledger/AccountLimitsFrame.h"
#include "database/Database.h"
#include "crypto/SecretKey.h"
#include "crypto/SHA.h"
#include "LedgerDelta.h"
#include "util/types.h"

using namespace std;
using namespace soci;

namespace stellar
{
const char* AccountLimitsFrame::kSQLCreateStatement1 =
    "CREATE TABLE account_limits"
    "("
    "accountid          VARCHAR(56)    PRIMARY KEY,"
    "daily_out          BIGINT         NOT NULL,"
	"weekly_out         BIGINT         NOT NULL,"
	"monthly_out        BIGINT         NOT NULL,"
    "annual_out         BIGINT         NOT NULL,"
    "lastmodified       INT            NOT NULL,"
    "version            INT            NOT NULL     DEFAULT 0"
    ");";

AccountLimitsFrame::AccountLimitsFrame() : EntryFrame(LedgerEntryType::ACCOUNT_TYPE_LIMITS), mAccountLimits(mEntry.data.accountLimits())
{
}

AccountLimitsFrame::AccountLimitsFrame(LedgerEntry const& from)
    : EntryFrame(from), mAccountLimits(mEntry.data.accountLimits())
{
}

AccountLimitsFrame::AccountLimitsFrame(AccountLimitsFrame const& from) : AccountLimitsFrame(from.mEntry)
{
}

AccountLimitsFrame& AccountLimitsFrame::operator=(AccountLimitsFrame const& other)
{
    if (&other != this)
    {
        mAccountLimits = other.mAccountLimits;
        mKey = other.mKey;
        mKeyCalculated = other.mKeyCalculated;
    }
    return *this;
}

bool
AccountLimitsFrame::isValid(AccountLimitsEntry const& oe)
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
AccountLimitsFrame::isValid() const
{
    return isValid(mAccountLimits);
}


uint64_t
AccountLimitsFrame::countObjects(soci::session& sess)
{
    uint64_t count = 0;
    sess << "SELECT COUNT(*) FROM account_type_limits;", into(count);
    return count;
}

void
AccountLimitsFrame::storeDelete(LedgerDelta& delta, Database& db) const
{
    storeDelete(delta, db, getKey());
}

void
AccountLimitsFrame::storeDelete(LedgerDelta& delta, Database& db, LedgerKey const& key)
{
    return;
}

void
AccountLimitsFrame::storeChange(LedgerDelta& delta, Database& db)
{
    storeUpdateHelper(delta, db, false);
}

void
AccountLimitsFrame::storeAdd(LedgerDelta& delta, Database& db)
{
    storeUpdateHelper(delta, db, true);
}

void
AccountLimitsFrame::storeUpdateHelper(LedgerDelta& delta, Database& db, bool insert)
{
    touch(delta);
    flushCachedEntry(db);
        
    if (!isValid())
    {
        throw std::runtime_error("Invalid AccountLimits state");
    }
    
    string sql;
        
    if (insert)
    {
        sql = std::string(
            "INSERT INTO account_limits ( accountid, daily_out, weekly_out, "
            "monthly_out, annual_out, lastmodified, version) "
            "VALUES ( :id, :v2, :v3, :v4, :v6, :v7, :v8)");
    }
    else
    {
        sql = std::string(
            "UPDATE account_limits "
            "SET    daily_out=:v2, weekly_out=:v3, monthly_out=:v4, annual_out=:v6, "
            "       lastmodified=:v7, version=:v8 "
            "WHERE  accountid = :id");
    }
    
    auto prep = db.getPreparedStatement(sql);
    auto& st = prep.statement();
    std::string actIDStrKey = PubKeyUtils::toStrKey(mAccountLimits.accountID);
    auto limits = mAccountLimits.limits;
    int32_t limitsVersion = static_cast<int32_t >(mAccountLimits.ext.v());
    st.exchange(use(actIDStrKey, "id"));
    st.exchange(use(limits.dailyOut, "v2"));
    st.exchange(use(limits.weeklyOut, "v3"));
    st.exchange(use(limits.monthlyOut, "v4"));
    st.exchange(use(limits.annualOut, "v6"));
    st.exchange(use(getLastModified(), "v7"));
    st.exchange(use(limitsVersion, "v8"));

    st.define_and_bind();
    
    auto timer =
    insert ? db.getInsertTimer("account-limits") : db.getUpdateTimer("account-limits");
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

AccountLimitsFrame::pointer AccountLimitsFrame::loadLimits(AccountID accountID,
    Database& db, LedgerDelta* delta)
{
    LedgerKey key;
    key.type(LedgerEntryType::ACCOUNT_LIMITS);
    key.accountLimits().accountID = accountID;
    if (cachedEntryExists(key, db))
    {
        auto p = getCachedEntry(key, db);
        return p ? make_shared<AccountLimitsFrame>(*p) : nullptr;
    }

    LedgerEntry le;
    le.data.type(LedgerEntryType::ACCOUNT_LIMITS);
    le.data.accountLimits().accountID = accountID;
    
    std::string actIDStrKey = PubKeyUtils::toStrKey(accountID);
    
    Limits limits;
    int32_t limitsVersion = 0;
    
    auto prep = db.getPreparedStatement("SELECT daily_out, weekly_out, monthly_out, annual_out, "
                                               "lastmodified, version "
                                        "FROM   account_limits "
                                        "WHERE  accountid=:id");
    auto& st = prep.statement();
    st.exchange(use(actIDStrKey));
    st.exchange(into(limits.dailyOut));
    st.exchange(into(limits.weeklyOut));
    st.exchange(into(limits.monthlyOut));
    st.exchange(into(limits.annualOut));
    st.exchange(into(le.lastModifiedLedgerSeq));
    st.exchange(into(limitsVersion));

    st.define_and_bind();
    {
        auto timer = db.getSelectTimer("account-limits");
        st.execute(true);
    }

    if (!st.got_data())
    {
        putCachedEntry(key, nullptr, db);
        return nullptr;
    }

    le.data.accountLimits().limits = limits;
    le.ext.v((LedgerVersion)limitsVersion);
    auto res = make_shared<AccountLimitsFrame>(le);
    assert(res->isValid());
    res->mKeyCalculated = false;
    res->putCachedEntry(db);
    if (delta)
    {
        delta->recordEntry(*res);
    }
    return res;
}

AccountLimitsFrame::pointer
AccountLimitsFrame::createNew(AccountID accountID, Limits limits)
{
    LedgerEntry le;
    le.data.type(LedgerEntryType::ACCOUNT_LIMITS);
    AccountLimitsEntry& entry = le.data.accountLimits();

    entry.accountID = accountID;
    entry.limits = limits;
    auto accountLimitsFrame = std::make_shared<AccountLimitsFrame>(le);
    return accountLimitsFrame;
}




bool
AccountLimitsFrame::exists(Database& db, LedgerKey const& key)
{
    return false;
}

void
AccountLimitsFrame::dropAll(Database& db)
{
    db.getSession() << "DROP TABLE IF EXISTS account_limits;";
    db.getSession() << kSQLCreateStatement1;
}
}
