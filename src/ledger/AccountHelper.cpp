// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ledger/AccountHelper.h"
#include "ledger/AccountTypeLimitsFrame.h"

#include "LedgerDelta.h"
#include "util/basen.h"
#include "util/types.h"
#include "lib/util/format.h"

using namespace soci;
using namespace std;

namespace stellar
{
	using xdr::operator<;

	void
	AccountHelper::storeUpdate(LedgerDelta& delta, Database& db, bool insert, LedgerEntry const& entry)
	{
		auto accountFrame = make_shared<AccountFrame>(entry);
		auto accountEntry = accountFrame->getAccount();

		bool isValid = accountFrame->isValid();
		assert(isValid);

		accountFrame->touch(delta);

		LedgerKey const& key = accountFrame->getKey();
		flushCachedEntry(key, db);

		std::string actIDStrKey = PubKeyUtils::toStrKey(accountFrame->getID());
        std::string recIdStrKey = PubKeyUtils::toStrKey(accountFrame->getRecoveryID());
		std::string refIDStrKey = "";
		AccountID* referrer = accountFrame->getReferrer();
		if (referrer)
			refIDStrKey = PubKeyUtils::toStrKey(*referrer);

		int32_t newAccountVersion = static_cast<int32_t>(accountFrame->getAccount().ext.v());
		int32_t newAccountPolicies = accountFrame->getPolicies();
		//set kyc level
		uint32 kycLevel = accountFrame->getKYCLevel();
		std::string sql;

        if (insert)
        {
            sql = std::string(
                "INSERT INTO accounts (accountid, recoveryid, thresholds, lastmodified, account_type, account_role,"
                "block_reasons, referrer, policies, kyc_level, version) "
                "VALUES (:id, :rid, :th, :lm, :type, :ar, :br, :ref, :p, :kyc, :v)");
        }
        else
        {
            sql = std::string(
                "UPDATE accounts "
                "SET    recoveryid=:rid, thresholds=:th, lastmodified=:lm, account_type=:type, account_role=:ar, "
                "       block_reasons=:br, referrer=:ref, policies=:p, kyc_level=:kyc, version=:v "
                "WHERE  accountid=:id");
        }

		auto prep = db.getPreparedStatement(sql);

		int32 accountType = static_cast<int32_t >(accountFrame->getAccountType());

		string thresholds(bn::encode_b64(accountFrame->getAccount().thresholds));

		{
			soci::statement& st = prep.statement();
			st.exchange(use(actIDStrKey, "id"));
            st.exchange(use(recIdStrKey, "rid"));
			st.exchange(use(thresholds, "th"));
			st.exchange(use(accountFrame->mEntry.lastModifiedLedgerSeq, "lm"));
			st.exchange(use(accountType, "type"));
			st.exchange(use(accountEntry.blockReasons, "br"));
			st.exchange(use(refIDStrKey, "ref"));
			st.exchange(use(newAccountPolicies, "p"));
			st.exchange(use(kycLevel, "kyc"));
			st.exchange(use(newAccountVersion, "v"));

            indicator roleIndicator = indicator::i_null;
            uint32 roleValue = 0;
            if (accountFrame->getAccountRole())
            {
                roleIndicator = indicator::i_ok;
                roleValue = *accountFrame->getAccountRole();
            }
            st.exchange(use(roleValue, roleIndicator, "ar"));

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
				delta.addEntry(*accountFrame);
			}
			else
			{
				delta.modEntry(*accountFrame);
			}
		}

		bool updateSigners = accountFrame->getUpdateSigners();
		if (updateSigners)
		{
			applySigners(db, insert, delta, accountFrame->mEntry);
		}
	}

	std::vector<Signer>
	AccountHelper::loadSigners(Database& db, std::string const& actIDStrKey)
	{
		std::vector<Signer> res;
		string pubKey, signerName;
		int32_t signerVersion;
		Signer signer;

		auto prep2 = db.getPreparedStatement("SELECT publickey, weight, signer_type, identity_id, signer_name, version "
			"FROM   signers "
			"WHERE  accountid =:id");
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

	void
	AccountHelper::applySigners(Database& db, bool insert, LedgerDelta& delta, LedgerEntry const& entry)
	{
		AccountFrame::pointer account = std::make_shared<AccountFrame>(entry);
		AccountEntry& accountEntry = account->getAccount();
		std::string actIDStrKey = PubKeyUtils::toStrKey(accountEntry.accountID);

		bool changed = false;
		// first, load the signers stored in the database for this account
		std::vector<Signer> oldSigners;
		if (!insert)
		{
			oldSigners = loadSigners(db, actIDStrKey);
		}

		auto it_new = accountEntry.signers.begin();
		auto it_old = oldSigners.begin();
		// iterate over both sets from smallest to biggest key
		while (it_new != accountEntry.signers.end() || it_old != oldSigners.end())
		{
			bool updated = false, added = false;

			if (it_old == oldSigners.end())
			{
				added = true;
			}
			else if (it_new != accountEntry.signers.end())
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
			LedgerKey const& key = account->getKey();
			flushCachedEntry(key, db);
		}
	}

	void AccountHelper::deleteSigner(Database& db, std::string const& accountID, AccountID const& pubKey) {
		std::string signerStrKey = PubKeyUtils::toStrKey(pubKey);
		auto prep = db.getPreparedStatement("DELETE FROM signers "
			"WHERE accountid=:v2 AND publickey=:v3");
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

	void AccountHelper::signerStoreChange(Database& db, LedgerDelta& delta, std::string const& accountID, std::vector<Signer>::iterator const& signer, bool insert) {
		int32_t signerVersion = static_cast<int32_t >(signer->ext.v());
		std::string newSignerName = signer->name;

		std::string signerStrKey = PubKeyUtils::toStrKey(signer->pubKey);
		auto timer = insert ? db.getInsertTimer("signer") : db.getUpdateTimer("signer");

		std::string sql;
		if (insert) {
			sql = std::string(
				"INSERT INTO signers (accountid, publickey, weight, signer_type,"
				" identity_id, signer_name, version) "
				"VALUES (:account_id, :pub_key, :weight, :type, :identity_id, :name, :version)");
		}
		else {
			sql = std::string(
				"UPDATE signers "
				"SET    weight=:weight, signer_type=:type, identity_id=:identity_id, "
				"       signer_name=:name, version=:version "
				"WHERE  accountid=:account_id AND publickey=:pub_key");
		}

		auto prep = db.getPreparedStatement(sql);
		auto& st = prep.statement();
		st.exchange(use(accountID, "account_id"));
		st.exchange(use(signerStrKey, "pub_key"));
		st.exchange(use(signer->weight, "weight"));
		st.exchange(use(signer->signerType, "type"));
		st.exchange(use(signer->identity, "identity_id"));
		st.exchange(use(newSignerName, "name"));
		st.exchange(use(signerVersion, "version"));
		st.define_and_bind();
		st.execute(true);

		if (st.get_affected_rows() != 1)
		{
			throw std::runtime_error("Could not update data in SQL");
		}
	}
	void
		AccountHelper::addKYCLevel(Database & db) {
		db.getSession() << "ALTER TABLE accounts ADD kyc_level INT DEFAULT 0";
	}
	void AccountHelper::addAccountRole(Database& db)
    {
        db.getSession() << "ALTER TABLE accounts ADD account_role BIGINT "
                           "REFERENCES account_roles(role_id) ON DELETE CASCADE "
						   "ON UPDATE CASCADE";
    }
	void
	AccountHelper::dropAll(Database& db)
	{
		db.getSession() << "DROP TABLE IF EXISTS accounts;";
		db.getSession() << "DROP TABLE IF EXISTS signers;";

		db.getSession() << "CREATE TABLE accounts"
			"("
			"accountid          VARCHAR(56)  PRIMARY KEY,"
            "recoveryid         VARCHAR(56)  NOT NULL,"
			"thresholds         TEXT         NOT NULL,"
			"lastmodified       INT          NOT NULL,"
			"account_type       INT          NOT NULL,"
			"block_reasons      INT          NOT NULL,"
			"referrer           VARCHAR(56)  NOT NULL,"
			"policies           INT          NOT NULL           DEFAULT 0,"
			"version            INT          NOT NULL           DEFAULT 0"
			");";
		db.getSession() << "CREATE TABLE signers"
			"("
			"accountid       VARCHAR(56)    NOT NULL,"
			"publickey       VARCHAR(56)    NOT NULL,"
			"weight          INT            NOT NULL,"
			"signer_type     INT            NOT NULL,"
			"identity_id     INT            NOT NULL,"
			"signer_name     VARCHAR(256)   NOT NULL    DEFAULT '',"
			"version         INT            NOT NULL    DEFAULT 0,"
			"PRIMARY KEY (accountid, publickey)"
			");";
		db.getSession() << "CREATE INDEX signersaccount ON signers (accountid)";
	}

	void
	AccountHelper::storeAdd(LedgerDelta& delta, Database& db, LedgerEntry const& entry)
	{
		storeUpdate(delta, db, true, entry);
	}

	void
	AccountHelper::storeChange(LedgerDelta& delta, Database& db, LedgerEntry const& entry)
	{
		storeUpdate(delta, db, false, entry);
	}

	void
	AccountHelper::storeDelete(LedgerDelta& delta, Database& db,
			LedgerKey const& key)
	{
		flushCachedEntry(key, db);

		std::string actIDStrKey = PubKeyUtils::toStrKey(key.account().accountID);
		{
			auto timer = db.getDeleteTimer("account");
			auto prep = db.getPreparedStatement("DELETE FROM accounts "
				"WHERE       accountid=:v1");
			auto& st = prep.statement();
			st.exchange(soci::use(actIDStrKey));
			st.define_and_bind();
			st.execute(true);
		}
		{
			auto timer = db.getDeleteTimer("signer");
			auto prep =
				db.getPreparedStatement("DELETE FROM signers "
					"WHERE       accountid=:v1");
			auto& st = prep.statement();
			st.exchange(soci::use(actIDStrKey));
			st.define_and_bind();
			st.execute(true);
		}
		delta.deleteEntry(key);
	}

	bool
	AccountHelper::exists(Database& db, LedgerKey const& key)
	{
		if (cachedEntryExists(key, db) && getCachedEntry(key, db) != nullptr)
		{
			return true;
		}

		return exists(key.account().accountID, db);
	}

	LedgerKey
	AccountHelper::getLedgerKey(LedgerEntry const& from)
	{
		LedgerKey ledgerKey;
		ledgerKey.type(from.data.type());
		ledgerKey.account().accountID = from.data.account().accountID;
		return ledgerKey;
	}

	EntryFrame::pointer AccountHelper::storeLoad(LedgerKey const& key, Database& db)
	{
		return loadAccount(key.account().accountID, db);
	}

	EntryFrame::pointer
	AccountHelper::fromXDR(LedgerEntry const& from)
	{
		return std::make_shared<AccountFrame>(from);
	}

	uint64_t
	AccountHelper::countObjects(soci::session& sess)
	{
		uint64_t count = 0;
		sess << "SELECT COUNT(*) FROM accounts;", into(count);
		return count;
	}

	AccountFrame::pointer
	AccountHelper::loadAccount(AccountID const& accountID, Database& db, LedgerDelta* delta)
	{
		LedgerKey key;
		key.type(LedgerEntryType::ACCOUNT);
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
		uint64 accountRole;
		soci::indicator isAccountRolePresent;
		uint32 accountPolicies;
		uint32 kycLevel;
		int32_t accountVersion;
		auto prep =
			db.getPreparedStatement("SELECT recoveryid, thresholds, lastmodified, account_type, account_role, "
				"block_reasons, referrer, policies, kyc_level, version "
				"FROM   accounts "
				"WHERE  accountid=:v1");
		auto& st = prep.statement();
        st.exchange(into(account.recoveryID));
		st.exchange(into(thresholds));
		st.exchange(into(res->mEntry.lastModifiedLedgerSeq));
		st.exchange(into(accountType));
		st.exchange(into(accountRole, isAccountRolePresent));
		st.exchange(into(account.blockReasons));
		st.exchange(into(referrer));
		st.exchange(into(accountPolicies));
		st.exchange(into(kycLevel));
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
		res->setKYCLevel(kycLevel);
		if (isAccountRolePresent == soci::i_null)
		{
			res->setAccountRole(nullptr);
		}
		else
		{
			res->setAccountRole(xdr::pointer<uint64>(new uint64(accountRole)));
		}
		if (referrer != "")
			account.referrer.activate() = PubKeyUtils::fromStrKey(referrer);
		bn::decode_b64(thresholds.begin(), thresholds.end(),
			res->getAccount().thresholds.begin());

		account.signers.clear();

		auto signers = loadSigners(db, actIDStrKey);
		account.signers.insert(account.signers.begin(), signers.begin(), signers.end());

		res->initLoaded(false);

		std::shared_ptr<LedgerEntry const> pEntry = std::make_shared<LedgerEntry const>(res->mEntry);
		putCachedEntry(key, pEntry, db);
		return res;
	}

	AccountFrame::pointer
	AccountHelper::loadAccount(LedgerDelta& delta, AccountID const& accountID,
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
	AccountHelper::mustLoadAccount(AccountID const& accountID, Database& db)
	{
		auto accountFrame = loadAccount(accountID, db);

		if (!accountFrame)
		{
			throw new::runtime_error("Expect account to exist");
		}

		return accountFrame;
	}

	std::unordered_map<AccountID, AccountFrame::pointer>
	AccountHelper::checkDB(Database& db)
	{
		std::unordered_map<AccountID, AccountFrame::pointer> state;
		{
			std::string id;
			soci::statement st =
				(db.getSession().prepare << "SELECT accountid FROM accounts",
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
			s.second = loadAccount(s.first, db);
		}

		{
			std::string id;
			size_t n;
			// sanity check signers state
			soci::statement st =
				(db.getSession().prepare << "SELECT count(*), accountid FROM "
					"signers GROUP BY accountid",
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
				else if (n != it->second->getAccount().signers.size())
				{
					throw std::runtime_error(
						fmt::format("Mismatch signers for account {}", id));
				}
				st.fetch();
			}
		}
		return state;
	}

	bool AccountHelper::exists(AccountID const &rawAccountID, Database &db) {
		int exists = 0;
		{
			auto timer = db.getSelectTimer("account-exists");
			auto prep =
					db.getPreparedStatement("SELECT EXISTS (SELECT NULL FROM accounts "
													"WHERE accountid=:v1)");
			auto& st = prep.statement();
                        auto accountID = PubKeyUtils::toStrKey(rawAccountID);
			st.exchange(use(accountID));
			st.exchange(into(exists));
			st.define_and_bind();
			st.execute(true);
		}
		return exists != 0;
	}

	void AccountHelper::ensureExists(AccountID const &accountID, Database &db) {
		if (!exists(accountID, db))
		{
			auto accountIdStr = PubKeyUtils::toStrKey(accountID);
			CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected state: account not found in database, accountID: "
												   << accountIdStr;
			throw runtime_error("Unexpected state: failed to found account in database, accountID: " + accountIdStr);
		}
	}
}