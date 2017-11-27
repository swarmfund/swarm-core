// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "AccountFrame.h"
#include "ledger/AccountTypeLimitsFrame.h"
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

const char* AccountFrame::kSQLCreateStatement1 =
    "CREATE TABLE accounts"
    "("
    "accountid         VARCHAR(56)  PRIMARY KEY,"
    "thresholds        TEXT         NOT NULL,"
    "lastmodified      INT          NOT NULL,"
	"account_type      INT          NOT NULL,"
	"block_reasons     INT          NOT NULL,"
    "referrer       VARCHAR(56) NOT NULL,"
	"share_for_referrer     BIGINT          NOT NULL"
    ");";

const char* AccountFrame::kSQLCreateStatement2 =
    "CREATE TABLE signers"
    "("
    "accountid       VARCHAR(56) NOT NULL,"
    "publickey       VARCHAR(56) NOT NULL,"
    "weight          INT         NOT NULL,"
	"signer_type     INT         NOT NULL,"
	"identity_id     INT         NOT NULL,"
    "PRIMARY KEY (accountid, publickey)"
    ");";

const char* AccountFrame::kSQLCreateStatement3 =
    "CREATE INDEX signersaccount ON signers (accountid)";

const char* AccountFrame::kSQLAddSignerName =
	"ALTER TABLE signers ADD signer_name VARCHAR(256) NOT NULL DEFAULT ''";

AccountFrame::AccountFrame()
    : EntryFrame(ACCOUNT), mAccountEntry(mEntry.data.account())
{
    mAccountEntry.thresholds[0] = 1; // by default, master key's weight is 1
    mUpdateSigners = true;
}

AccountFrame::AccountFrame(LedgerEntry const& from)
    : EntryFrame(from), mAccountEntry(mEntry.data.account())
{
    // we cannot make any assumption on mUpdateSigners:
    // it's possible we're constructing an account with no signers
    // but that the database's state had a previous version with signers
    mUpdateSigners = true;
    mAccountEntry.limits = nullptr;
}

AccountFrame::AccountFrame(AccountFrame const& from) : AccountFrame(from.mEntry)
{
}

AccountFrame::AccountFrame(AccountID const& id) : AccountFrame()
{
    mAccountEntry.accountID = id;
}

AccountFrame::pointer
AccountFrame::makeAuthOnlyAccount(AccountID const& id)
{
    AccountFrame::pointer ret = make_shared<AccountFrame>(id);
    return ret;
}

bool
AccountFrame::signerCompare(Signer const& s1, Signer const& s2)
{
    return s1.pubKey < s2.pubKey;
}

void
AccountFrame::normalize()
{
    std::sort(mAccountEntry.signers.begin(), mAccountEntry.signers.end(),
              &AccountFrame::signerCompare);
}

bool
AccountFrame::isValid()
{
	// if we do not have referrer, share for referrer must be 0
	if (!mAccountEntry.referrer && mAccountEntry.shareForReferrer != 0)
		return false;

	if (mAccountEntry.shareForReferrer < 0)
		return false;

	// it is not valid behaviour if we have shareForReferrer == 100%. DO NOT EDIT!
	if (mAccountEntry.shareForReferrer >= int64(100 * ONE))
		return false;

    auto const& a = mAccountEntry;
    return std::is_sorted(a.signers.begin(), a.signers.end(),
                          &AccountFrame::signerCompare);
}

bool AccountFrame::isBlocked() const
{
	return mAccountEntry.blockReasons > 0;
}

void AccountFrame::setBlockReasons(uint32 reasonsToAdd, uint32 reasonsToRemove) const
{
	mAccountEntry.blockReasons |= reasonsToAdd;
    mAccountEntry.blockReasons &= ~reasonsToRemove;
}


AccountID const&
AccountFrame::getID() const
{
    return (mAccountEntry.accountID);
}


uint32_t
AccountFrame::getMasterWeight() const
{
    return mAccountEntry.thresholds[THRESHOLD_MASTER_WEIGHT];
}

uint32_t
AccountFrame::getHighThreshold() const
{
    return mAccountEntry.thresholds[THRESHOLD_HIGH];
}

uint32_t
AccountFrame::getMediumThreshold() const
{
    return mAccountEntry.thresholds[THRESHOLD_MED];
}

uint32_t
AccountFrame::getLowThreshold() const
{
    return mAccountEntry.thresholds[THRESHOLD_LOW];
}

AccountFrame::pointer
AccountFrame::loadAccount(LedgerDelta& delta, AccountID const& accountID,
                          Database& db)
{
    auto a = loadAccount(accountID, db);
    if (a)
    {
        delta.recordEntry(*a);
    }
    return a;
}


AccountFrame::pointer
AccountFrame::loadAccount(AccountID const& accountID, Database& db, LedgerDelta* delta)
{
    LedgerKey key;
    key.type(ACCOUNT);
    key.account().accountID = accountID;
    if (cachedEntryExists(key, db))
    {
        auto p = getCachedEntry(key, db);
        return p ? std::make_shared<AccountFrame>(*p) : nullptr;
    }

    std::string actIDStrKey = PubKeyUtils::toStrKey(accountID);

    std::string publicKey, creditAuthKey, referrer;
    std::string thresholds;

    AccountFrame::pointer res = make_shared<AccountFrame>(accountID);
    AccountEntry& account = res->getAccount();

	int32 accountType;
	uint32 accountPolicies;
	int32_t accountVersion;
    auto prep =
        db.getPreparedStatement("SELECT "
                                "thresholds, "
                                "lastmodified, account_type, block_reasons,"
                                "referrer, share_for_referrer, policies, version "
                                "FROM accounts WHERE accountid=:v1");
    auto& st = prep.statement();
    st.exchange(into(thresholds));
    st.exchange(into(res->getLastModified()));
	st.exchange(into(accountType));
	st.exchange(into(account.blockReasons));
	st.exchange(into(referrer));
    st.exchange(into(account.shareForReferrer));
	st.exchange(into(accountPolicies));
	st.exchange(into(accountVersion));
    st.exchange(use(actIDStrKey));
    st.define_and_bind();
    {
        auto timer = db.getSelectTimer("account");
        st.execute(true);
    }

    if (!st.got_data())
    {
        putCachedEntry(key, nullptr, db);
        return nullptr;
    }
	account.accountType = AccountType(accountType);
	account.ext.v((LedgerVersion)accountVersion);
	account.policies = accountPolicies;
	
    if (referrer != "")
        account.referrer.activate() = PubKeyUtils::fromStrKey(referrer);
    bn::decode_b64(thresholds.begin(), thresholds.end(),
                   res->mAccountEntry.thresholds.begin());


    account.signers.clear();


    auto signers = loadSigners(db, actIDStrKey, delta);
    account.signers.insert(account.signers.begin(), signers.begin(), signers.end());
    
    res->normalize();
    res->mUpdateSigners = false;
    assert(res->isValid());
    res->mKeyCalculated = false;
    res->putCachedEntry(db);
    return res;
}

AccountFrame::pointer
AccountFrame::mustLoadAccount(AccountID const& accountID, Database& db, LedgerDelta* delta)
{
	auto accountFrame = loadAccount(accountID, db, delta);

	if (!accountFrame)
	{
		throw new::runtime_error("Expect account to exist");
	}

	return accountFrame;
}


std::vector<Signer>
AccountFrame::loadSigners(Database& db, std::string const& actIDStrKey, LedgerDelta* delta)
{
    std::vector<Signer> res;
	string pubKey, signerName;
	int32_t signerVersion;
    Signer signer;

    auto prep2 = db.getPreparedStatement("SELECT publickey, weight, signer_type, identity_id, signer_name, version FROM "
                                         "signers WHERE accountid =:id");
    auto& st2 = prep2.statement();
    st2.exchange(use(actIDStrKey));
    st2.exchange(into(pubKey));
    st2.exchange(into(signer.weight));
	st2.exchange(into(signer.signerType));
	st2.exchange(into(signer.identity));
	st2.exchange(into(signerName));
	st2.exchange(into(signerVersion));
    st2.define_and_bind();
    {
        auto timer = db.getSelectTimer("signer");
        st2.execute(true);
    }
    while (st2.got_data())
    {
        signer.pubKey = PubKeyUtils::fromStrKey(pubKey);
		signer.name = signerName;
        res.push_back(signer);
        st2.fetch();
    }

    std::sort(res.begin(), res.end(), &AccountFrame::signerCompare);

    return res;
}

bool
AccountFrame::exists(Database& db, LedgerKey const& key)
{
    if (cachedEntryExists(key, db) && getCachedEntry(key, db) != nullptr)
    {
        return true;
    }

    std::string actIDStrKey = PubKeyUtils::toStrKey(key.account().accountID);
    int exists = 0;
    {
        auto timer = db.getSelectTimer("account-exists");
        auto prep =
            db.getPreparedStatement("SELECT EXISTS (SELECT NULL FROM accounts "
                                    "WHERE accountid=:v1)");
        auto& st = prep.statement();
        st.exchange(use(actIDStrKey));
        st.exchange(into(exists));
        st.define_and_bind();
        st.execute(true);
    }
    return exists != 0;
}

uint64_t
AccountFrame::countObjects(soci::session& sess)
{
    uint64_t count = 0;
    sess << "SELECT COUNT(*) FROM accounts;", into(count);
    return count;
}

void
AccountFrame::storeDelete(LedgerDelta& delta, Database& db) const
{
    storeDelete(delta, db, getKey());
}

void
AccountFrame::storeDelete(LedgerDelta& delta, Database& db,
                          LedgerKey const& key)
{
    flushCachedEntry(key, db);

    std::string actIDStrKey = PubKeyUtils::toStrKey(key.account().accountID);
    {
        auto timer = db.getDeleteTimer("account");
        auto prep = db.getPreparedStatement(
            "DELETE from accounts where accountid= :v1");
        auto& st = prep.statement();
        st.exchange(soci::use(actIDStrKey));
        st.define_and_bind();
        st.execute(true);
    }
    {
        auto timer = db.getDeleteTimer("signer");
        auto prep =
            db.getPreparedStatement("DELETE from signers where accountid= :v1");
        auto& st = prep.statement();
        st.exchange(soci::use(actIDStrKey));
        st.define_and_bind();
        st.execute(true);
    }
    delta.deleteEntry(key);
}



void
AccountFrame::storeUpdate(LedgerDelta& delta, Database& db, bool insert)
{
    assert(isValid());

    touch(delta);

    flushCachedEntry(db);

    std::string actIDStrKey = PubKeyUtils::toStrKey(mAccountEntry.accountID);
    std::string refIDStrKey = "";
    if (mAccountEntry.referrer)
        refIDStrKey = PubKeyUtils::toStrKey(*mAccountEntry.referrer);

	int32_t newAccountVersion = mAccountEntry.ext.v();
	uint32 newAccountPolicies = mAccountEntry.policies;

    std::string sql;

    if (insert)
    {
        sql = std::string(
            "INSERT INTO accounts ( accountid, "
            "thresholds, "
            "lastmodified, account_type, block_reasons,"
            "referrer, share_for_referrer, policies, version, created_at) "
            "VALUES ( :id, :v4, :v5, :v7, :v8, :v9, :v10, :v11, :v12, to_timestamp(:created_at))");
    }
    else
    {
        sql = std::string(
            "UPDATE accounts SET "
            "thresholds = :v4, "
            "lastmodified = :v5, account_type = :v7, block_reasons = :v8, "
			"referrer=:v9, share_for_referrer=:v10, policies=:v11, version=:v12 "
            " WHERE accountid = :id");
    }

    auto prep = db.getPreparedStatement(sql);

    int32 accountType = mAccountEntry.accountType;

    string thresholds(bn::encode_b64(mAccountEntry.thresholds));

    {
        soci::statement& st = prep.statement();
        st.exchange(use(actIDStrKey, "id"));
        st.exchange(use(thresholds, "v4"));
        st.exchange(use(getLastModified(), "v5"));
		st.exchange(use(accountType, "v7"));
		st.exchange(use(mAccountEntry.blockReasons, "v8"));
		st.exchange(use(refIDStrKey, "v9"));
        st.exchange(use(mAccountEntry.shareForReferrer, "v10"));
		st.exchange(use(newAccountPolicies, "v11"));
		st.exchange(use(newAccountVersion, "v12"));
		if (insert)
		{
			st.exchange(use(delta.getHeader().scpValue.closeTime, "created_at"));
		}
        st.define_and_bind();
        {
            auto timer = insert ? db.getInsertTimer("account")
                                : db.getUpdateTimer("account");
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

    if (mUpdateSigners)
    {
        applySigners(db, insert, delta);
    }
}

void AccountFrame::deleteSigner(Database& db, std::string const& accountID, AccountID const& pubKey) {
	std::string signerStrKey = PubKeyUtils::toStrKey(pubKey);
	auto prep = db.getPreparedStatement("DELETE from signers WHERE accountid=:v2 AND publickey=:v3");
	auto& st = prep.statement();
	st.exchange(use(accountID));
	st.exchange(use(signerStrKey));
	st.define_and_bind();
	{
		auto timer = db.getDeleteTimer("signer");
		st.execute(true);
	}

	if (st.get_affected_rows() != 1)
	{
		throw std::runtime_error("Could not update data in SQL");
	}
}

void AccountFrame::signerStoreChange(Database& db, LedgerDelta& delta, std::string const& accountID, std::vector<Signer>::iterator const& signer, bool insert) {
	int32_t newSignerVersion = signer->ext.v();
	std::string newSignerName = signer->name;

	std::string signerStrKey = PubKeyUtils::toStrKey(signer->pubKey);
	auto timer = insert ? db.getInsertTimer("signer") : db.getUpdateTimer("signer");
	auto prep = insert ? 
		db.getPreparedStatement("INSERT INTO signers "
			"(accountid,publickey,weight,signer_type,identity_id,signer_name,version) "
			"VALUES (:account_id,:pub_key,:weight,:type,:identity_id,:name,:version)")
		:
		db.getPreparedStatement(
		"UPDATE signers set weight=:weight, signer_type=:type, identity_id=:identity_id, signer_name=:name, version=:version WHERE "
		"accountid=:account_id AND publickey=:pub_key");

	auto& st = prep.statement();
	st.exchange(use(accountID, "account_id"));
	st.exchange(use(signerStrKey, "pub_key"));
	st.exchange(use(signer->weight, "weight"));
	st.exchange(use(signer->signerType, "type"));
	st.exchange(use(signer->identity, "identity_id"));
	st.exchange(use(newSignerName, "name"));
	st.exchange(use(newSignerVersion, "version"));
	st.define_and_bind();
	st.execute(true);

	if (st.get_affected_rows() != 1)
	{
		throw std::runtime_error("Could not update data in SQL");
	}

}


void
AccountFrame::applySigners(Database& db, bool insert, LedgerDelta& delta)
{
    std::string actIDStrKey = PubKeyUtils::toStrKey(mAccountEntry.accountID);

	bool changed = false;
    // first, load the signers stored in the database for this account
    std::vector<Signer> oldSigners;
    if (!insert)
    {
		oldSigners = loadSigners(db, actIDStrKey, &delta);
    }

	auto it_new = mAccountEntry.signers.begin();
    auto it_old = oldSigners.begin();
    // iterate over both sets from smallest to biggest key
    while (it_new != mAccountEntry.signers.end() || it_old != oldSigners.end())
    {
        bool updated = false, added = false;

        if (it_old == oldSigners.end())
        {
            added = true;
        }
        else if (it_new != mAccountEntry.signers.end())
        {
            updated = (it_new->pubKey == it_old->pubKey);
            if (!updated)
            {
                added = (it_new->pubKey < it_old->pubKey);
            }
        }
        
		// delete
		if (!updated && !added) {
			deleteSigner(db, actIDStrKey, it_old->pubKey);
			it_old++;
			changed = true;
			continue;
		}

		// add new
		if (added) {
			signerStoreChange(db, delta, actIDStrKey, it_new, true);
			changed = true;
			it_new++;
			continue;
		}

		// updated
		if (!(*it_new == *it_old))
		{
			signerStoreChange(db, delta, actIDStrKey, it_new, false);
			changed = true;
		}
		it_new++;
		it_old++;

		
    }

    if (changed)
    {
        // Flush again to ensure changed signers are reloaded.
        flushCachedEntry(db);
    }
}

void
AccountFrame::storeChange(LedgerDelta& delta, Database& db)
{
    storeUpdate(delta, db, false);
}

void
AccountFrame::storeAdd(LedgerDelta& delta, Database& db)
{
	storeUpdate(delta, db, true);
}

std::unordered_map<AccountID, AccountFrame::pointer>
AccountFrame::checkDB(Database& db)
{
    std::unordered_map<AccountID, AccountFrame::pointer> state;
    {
        std::string id;
        soci::statement st =
            (db.getSession().prepare << "select accountid from accounts",
             soci::into(id));
        st.execute(true);
        while (st.got_data())
        {
            state.insert(std::make_pair(PubKeyUtils::fromStrKey(id), nullptr));
            st.fetch();
        }
    }
    // load all accounts
    for (auto& s : state)
    {
        s.second = AccountFrame::loadAccount(s.first, db);
    }

    {
        std::string id;
        size_t n;
        // sanity check signers state
        soci::statement st =
            (db.getSession().prepare << "select count(*), accountid from "
                                        "signers group by accountid",
             soci::into(n), soci::into(id));
        st.execute(true);
        while (st.got_data())
        {
            AccountID aid(PubKeyUtils::fromStrKey(id));
            auto it = state.find(aid);
            if (it == state.end())
            {
                throw std::runtime_error(fmt::format(
                    "Found extra signers in database for account {}", id));
            }
            else if (n != it->second->mAccountEntry.signers.size())
            {
                throw std::runtime_error(
                    fmt::format("Mismatch signers for account {}", id));
            }
            st.fetch();
        }
    }
    return state;
}

void
AccountFrame::dropAll(Database& db)
{
    db.getSession() << "DROP TABLE IF EXISTS accounts;";
    db.getSession() << "DROP TABLE IF EXISTS signers;";

    db.getSession() << kSQLCreateStatement1;
    db.getSession() << kSQLCreateStatement2;
    db.getSession() << kSQLCreateStatement3;
}

void
AccountFrame::addSignerName(Database& db)
{
	db.getSession() << kSQLAddSignerName;
}

void
AccountFrame::addSignerVersion(Database& db)
{
	db.getSession() << "ALTER TABLE signers ADD version INT NOT NULL DEFAULT 0;";
	db.getSession() << "UPDATE signers SET version=6 WHERE char_length(signer_name)>0;";
}

void
AccountFrame::addAccountPolicies(Database& db)
{
	db.getSession() << "ALTER TABLE accounts ADD policies INT NOT NULL DEFAULT 0;";
	db.getSession() << "ALTER TABLE accounts ADD version INT NOT NULL DEFAULT 0;";
}

void AccountFrame::addCreatedAt(Database& db)
{
	db.getSession() << "ALTER TABLE accounts ADD COLUMN created_at TIMESTAMP WITHOUT TIME ZONE DEFAULT NOW();";
}
}
