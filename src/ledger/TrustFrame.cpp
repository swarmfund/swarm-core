// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "TrustFrame.h"
#include "crypto/SecretKey.h"
#include "crypto/Hex.h"
#include "database/Database.h"
#include "LedgerDelta.h"
#include "util/basen.h"
#include "util/types.h"
#include "lib/util/format.h"
#include <algorithm>

using namespace soci;
using namespace std;

namespace stellar
{
using xdr::operator<;

const char* TrustFrame::kSQLCreateStatement1 =
    "CREATE TABLE trusts"
    "("
    "allowed_account         VARCHAR(64)  NOT NULL,"
    "balance_to_use        VARCHAR(64)         NOT NULL,"
    "lastmodified      INT          NOT NULL,"
    "PRIMARY KEY (balance_to_use, allowed_account)"
    ");";


TrustFrame::TrustFrame()
    : EntryFrame(TRUST), mTrustEntry(mEntry.data.trust())
{
}

TrustFrame::TrustFrame(LedgerEntry const& from)
    : EntryFrame(from), mTrustEntry(mEntry.data.trust())
{
}

TrustFrame::TrustFrame(TrustFrame const& from) : TrustFrame(from.mEntry)
{
}

bool
TrustFrame::isValid()
{
    return true;
}


TrustFrame::pointer
TrustFrame::loadTrust(AccountID const& allowedAccount,
    BalanceID const& balanceToUse, Database& db)
{
    LedgerKey key;
    key.type(TRUST);
    key.trust().allowedAccount = allowedAccount;
    key.trust().balanceToUse = balanceToUse;
    if (cachedEntryExists(key, db))
    {
        auto p = getCachedEntry(key, db);
        return p ? std::make_shared<TrustFrame>(*p) : nullptr;
    }

    std::string actIDStrKey = PubKeyUtils::toStrKey(allowedAccount);
    std::string balIDStrKey = BalanceKeyUtils::toStrKey(balanceToUse);

    TrustFrame::pointer res = make_shared<TrustFrame>();
    TrustEntry& trust = res->getTrust();

    auto prep =
        db.getPreparedStatement("SELECT lastmodified "
                                "FROM trusts WHERE allowed_account=:v1 AND balance_to_use=:v2");
    auto& st = prep.statement();
    st.exchange(into(res->getLastModified()));
    st.exchange(use(actIDStrKey));
    st.exchange(use(balIDStrKey));
    st.define_and_bind();
    {
        auto timer = db.getSelectTimer("trusts");
        st.execute(true);
    }

    if (!st.got_data())
    {
        putCachedEntry(key, nullptr, db);
        return nullptr;
    }

    trust.allowedAccount = PubKeyUtils::fromStrKey(actIDStrKey);
    trust.balanceToUse = BalanceKeyUtils::fromStrKey(balIDStrKey);

    assert(res->isValid());
    res->mKeyCalculated = false;
    res->putCachedEntry(db);
    return res;
}


bool
TrustFrame::exists(Database& db, LedgerKey const& key)
{
    if (cachedEntryExists(key, db) && getCachedEntry(key, db) != nullptr)
    {
        return true;
    }

    std::string actIDStrKey = PubKeyUtils::toStrKey(key.trust().allowedAccount);
    std::string balIDStrKey = BalanceKeyUtils::toStrKey(key.trust().balanceToUse);
    int exists = 0;
    {
        auto timer = db.getSelectTimer("Trust-exists");
        auto prep =
            db.getPreparedStatement("SELECT EXISTS (SELECT NULL FROM trusts WHERE allowed_account=:v1 AND balance_to_use=:v2)");
        auto& st = prep.statement();
        st.exchange(use(actIDStrKey));
        st.exchange(use(balIDStrKey));
        st.exchange(into(exists));
        st.define_and_bind();
        st.execute(true);
    }
    return exists != 0;
}

bool
TrustFrame::exists(Database& db, AccountID allowedAccount, BalanceID balanceToUse)
{
    std::string actIDStrKey = PubKeyUtils::toStrKey(allowedAccount);
    std::string balIDStrKey = BalanceKeyUtils::toStrKey(balanceToUse);

    int exists = 0;
    {
        auto timer = db.getSelectTimer("Trust-exists");
        auto prep =
            db.getPreparedStatement("SELECT EXISTS (SELECT NULL FROM trusts WHERE allowed_account=:v1 AND balance_to_use=:v2)");
        auto& st = prep.statement();
        st.exchange(use(actIDStrKey));
        st.exchange(use(balIDStrKey));
        st.exchange(into(exists));
        st.define_and_bind();
        st.execute(true);
    }
    return exists != 0;
}


TrustFrame::pointer TrustFrame::createNew(AccountID allowedAccount, BalanceID balanceToUse)
{
	LedgerEntry le;
	le.data.type(TRUST);
	TrustEntry& entry = le.data.trust();

	entry.allowedAccount = allowedAccount;
	entry.balanceToUse = balanceToUse;
	return std::make_shared<TrustFrame>(le);
}


uint64_t
TrustFrame::countObjects(soci::session& sess)
{
    uint64_t count = 0;
    sess << "SELECT COUNT(*) FROM trusts;", into(count);
    return count;
}

uint64_t TrustFrame::countForBalance(Database & db, BalanceID balanceToUse)
{
	int total = 0;
	auto timer = db.getSelectTimer("balance-count");
	auto prep =
		db.getPreparedStatement("SELECT COUNT(*) FROM trusts "
			"WHERE balance_to_use=:receiver;");
	auto& st = prep.statement();

	std::string actIDStrKey;
	actIDStrKey = BalanceKeyUtils::toStrKey(balanceToUse);

	st.exchange(use(actIDStrKey));
	st.exchange(into(total));
	st.define_and_bind();
	st.execute(true);

	return total;
}

void
TrustFrame::storeDelete(LedgerDelta& delta, Database& db) const
{
    storeDelete(delta, db, getKey());
}

void
TrustFrame::storeDelete(LedgerDelta& delta, Database& db,
                          LedgerKey const& key)
{
    flushCachedEntry(key, db);

    std::string actIDStrKey = PubKeyUtils::toStrKey(key.trust().allowedAccount);
    std::string balIDStrKey = BalanceKeyUtils::toStrKey(key.trust().balanceToUse);
    {
        auto timer = db.getDeleteTimer("trusts");
        auto prep = db.getPreparedStatement(
            "DELETE from trusts where allowed_account=:v1 AND balance_to_use=:v2");
        auto& st = prep.statement();
        st.exchange(soci::use(actIDStrKey));
        st.exchange(soci::use(balIDStrKey));

        st.define_and_bind();
        st.execute(true);
    }
    delta.deleteEntry(key);
}

void
TrustFrame::storeUpdate(LedgerDelta& delta, Database& db, bool insert)
{
    assert(isValid());

    touch(delta);

    flushCachedEntry(db);

    std::string actIDStrKey = PubKeyUtils::toStrKey(mTrustEntry.allowedAccount);
    std::string balIDStrKey = BalanceKeyUtils::toStrKey(mTrustEntry.balanceToUse);
    std::string sql;

    if (insert)
    {
        sql = std::string(
            "INSERT INTO trusts ( allowed_account, balance_to_use, "
            "lastmodified) "
            "VALUES ( :id, :v2, :v3)");
    }
    else
    {
        sql = std::string(
            "UPDATE trusts SET lastmodified=:v3 WHERE allowed_account=:id AND balance_to_use=:v2");
    }

    auto prep = db.getPreparedStatement(sql);

    {        
        soci::statement& st = prep.statement();
        st.exchange(use(actIDStrKey, "id"));
        st.exchange(use(balIDStrKey, "v2"));
        st.exchange(use(mEntry.lastModifiedLedgerSeq, "v3"));
        st.define_and_bind();
        {
            auto timer = insert ? db.getInsertTimer("trusts")
                                : db.getUpdateTimer("trusts");
            st.execute(true);
        }

        if (st.get_affected_rows() != 1)
        {
            throw std::runtime_error("Could not update data in SQL");
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

}


void
TrustFrame::storeChange(LedgerDelta& delta, Database& db)
{
    storeUpdate(delta, db, false);
}

void
TrustFrame::storeAdd(LedgerDelta& delta, Database& db)
{
    storeUpdate(delta, db, true);
}

void
TrustFrame::dropAll(Database& db)
{
    db.getSession() << "DROP TABLE IF EXISTS trusts;";

    db.getSession() << kSQLCreateStatement1;
}
}
