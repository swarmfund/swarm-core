// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "AccountLimitsHelper.h"
#include "database/Database.h"
#include "crypto/SecretKey.h"
#include "crypto/SHA.h"
#include "LedgerDelta.h"
#include "util/types.h"

using namespace std;
using namespace soci;

namespace stellar
{
	void
	AccountLimitsHelper::dropAll(Database& db)
	{
		db.getSession() << "DROP TABLE IF EXISTS account_limits;";
		db.getSession() << "CREATE TABLE account_limits"
			"("
			"accountid          VARCHAR(56)    PRIMARY KEY,"
			"daily_out          BIGINT         NOT NULL,"
			"weekly_out         BIGINT         NOT NULL,"
			"monthly_out        BIGINT         NOT NULL,"
			"annual_out         BIGINT         NOT NULL,"
			"lastmodified       INT            NOT NULL,"
			"version            INT            NOT NULL     DEFAULT 0"
			");";
	}

	void
	AccountLimitsHelper::storeUpdateHelper(LedgerDelta& delta, Database& db, bool insert, LedgerEntry const& entry)
	{
		auto accountLimitsFrame = make_shared<AccountLimitsFrame>(entry);
		auto accountLimitsEntry = accountLimitsFrame->getAccountLimits();

		accountLimitsFrame->touch(delta);

		auto key = accountLimitsFrame->getKey();
		flushCachedEntry(key, db);

		bool isValid = accountLimitsFrame->isValid();
		if (!isValid)
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
		std::string actIDStrKey = PubKeyUtils::toStrKey(accountLimitsEntry.accountID);
		auto limits = accountLimitsFrame->getLimits();
		int32_t limitsVersion = static_cast<int32_t >(accountLimitsEntry.ext.v());
		st.exchange(use(actIDStrKey, "id"));
		st.exchange(use(limits.dailyOut, "v2"));
		st.exchange(use(limits.weeklyOut, "v3"));
		st.exchange(use(limits.monthlyOut, "v4"));
		st.exchange(use(limits.annualOut, "v6"));
		st.exchange(use(accountLimitsFrame->mEntry.lastModifiedLedgerSeq, "v7"));
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
			delta.addEntry(*accountLimitsFrame);
		}
		else
		{
			delta.modEntry(*accountLimitsFrame);
		}
	}

	void 
	AccountLimitsHelper::storeAdd(LedgerDelta& delta, Database& db, LedgerEntry const& entry)
	{
		storeUpdateHelper(delta, db, true, entry);
	}

	void
	AccountLimitsHelper::storeChange(LedgerDelta& delta, Database& db, LedgerEntry const& entry)
	{
		storeUpdateHelper(delta, db, false, entry);
	}

	void
	AccountLimitsHelper::storeDelete(LedgerDelta& delta, Database& db, LedgerKey const& key)
	{
		throw new std::runtime_error("AccountLimitsFrame is not supposed to be deleted");
	}

	bool
	AccountLimitsHelper::exists(Database& db, LedgerKey const& key)
	{
		return false;
	}

	LedgerKey
	AccountLimitsHelper::getLedgerKey(LedgerEntry const& from)
	{
		LedgerKey ledgerKey;
		ledgerKey.type(from.data.type());
		ledgerKey.accountLimits().accountID = from.data.accountLimits().accountID;
		return ledgerKey;
	}

	EntryFrame::pointer
	AccountLimitsHelper::storeLoad(LedgerKey const& key, Database& db)
	{
		if (cachedEntryExists(key, db)) {
			auto p = getCachedEntry(key, db);
			return p ? make_shared<AccountLimitsFrame>(*p) : nullptr;
		}

		LedgerEntry le;
		le.data.type(LedgerEntryType::ACCOUNT_LIMITS);
		le.data.accountLimits().accountID = key.accountLimits().accountID;

		std::string actIDStrKey = PubKeyUtils::toStrKey(key.accountLimits().accountID);

		Limits limits;
		int32_t limitsVersion = 0;

		auto prep = db.getPreparedStatement(
			"SELECT daily_out, weekly_out, monthly_out, annual_out, lastmodified, version FROM account_limits WHERE accountid =:id");
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

		if (!st.got_data()) {
			putCachedEntry(key, nullptr, db);
			return nullptr;
		}

		le.data.accountLimits().limits = limits;
		le.ext.v((LedgerVersion)limitsVersion);
		auto res = make_shared<AccountLimitsFrame>(le);
		assert(res->isValid());
		res->clearCached();
		auto pEntry = std::make_shared<LedgerEntry>(res->mEntry);
		putCachedEntry(key, pEntry, db);
		return res;
	}

	EntryFrame::pointer
	AccountLimitsHelper::fromXDR(LedgerEntry const& from)
	{
		return std::make_shared<AccountLimitsFrame>(from);
	}

	uint64_t
	AccountLimitsHelper::countObjects(soci::session& sess)
	{
		uint64_t count = 0;
		sess << "SELECT COUNT(*) FROM account_type_limits;", into(count);
		return count;
	}

	AccountLimitsFrame::pointer 
	AccountLimitsHelper::loadLimits(AccountID accountID, Database& db, LedgerDelta* delta)
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
		res->clearCached();
		auto pEntry = std::make_shared<LedgerEntry>(res->mEntry);
		putCachedEntry(key, pEntry, db);
		if (delta)
		{
			delta->recordEntry(*res);
		}
		return res;
	}

}