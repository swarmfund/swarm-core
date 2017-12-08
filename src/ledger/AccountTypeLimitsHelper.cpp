// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ledger/AccountTypeLimitsHelper.h"
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
	AccountTypeLimitsHelper::dropAll(Database& db)
	{
		db.getSession() << "DROP TABLE IF EXISTS account_type_limits;";
		db.getSession() << "CREATE TABLE account_type_limits"
			"("
			"account_type INT    NOT NULL PRIMARY KEY,"
			"daily_out          BIGINT NOT NULL,"
			"weekly_out        BIGINT NOT NULL,"
			"monthly_out        BIGINT NOT NULL,"
			"annual_out        BIGINT NOT NULL,"
			"lastmodified   INT    NOT NULL"
			");";
	}

	void
	AccountTypeLimitsHelper::storeUpdateHelper(LedgerDelta& delta, Database& db, bool insert, LedgerEntry const& entry)
	{
		auto accountTypeLimitsFrame = make_shared<AccountTypeLimitsFrame>(entry);
		auto accountTypeLimitsEntry = accountTypeLimitsFrame->getAccountTypeLimits();

		accountTypeLimitsFrame->touch(delta);
		auto key = accountTypeLimitsFrame->getKey();
		flushCachedEntry(key, db);

		bool isValid = accountTypeLimitsFrame->isValid();
		if (!isValid)
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
		int accountType = static_cast<int32_t >(accountTypeLimitsEntry.accountType);
		auto limits =accountTypeLimitsFrame->getLimits();
		st.exchange(use(accountType, "at"));
		st.exchange(use(limits.dailyOut, "do"));
		st.exchange(use(limits.weeklyOut, "wo"));
		st.exchange(use(limits.monthlyOut, "mo"));
		st.exchange(use(limits.annualOut, "ao"));
		st.exchange(use(accountTypeLimitsFrame->mEntry.lastModifiedLedgerSeq, "lm"));
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
			delta.addEntry(*accountTypeLimitsFrame);
		}
		else
		{
			delta.modEntry(*accountTypeLimitsFrame);
		}
	}

	void
	AccountTypeLimitsHelper::storeAdd(LedgerDelta& delta, Database& db, LedgerEntry const& entry)
	{
		storeUpdateHelper(delta, db, true, entry);
	}

	void
	AccountTypeLimitsHelper::storeChange(LedgerDelta& delta, Database& db, LedgerEntry const& entry)
	{
		storeUpdateHelper(delta, db, false, entry);
	}

	void
	AccountTypeLimitsHelper::storeDelete(LedgerDelta& delta, Database& db, LedgerKey const& key)
	{
		return;
	}

	bool
	AccountTypeLimitsHelper::exists(Database& db, LedgerKey const& key)
	{
		return false;
	}

	LedgerKey
	AccountTypeLimitsHelper::getLedgerKey(LedgerEntry const &from)
	{
		LedgerKey ledgerKey;
		ledgerKey.type(from.data.type());
		ledgerKey.accountTypeLimits().accountType = from.data.accountTypeLimits().accountType;
		return ledgerKey;
	}

	EntryFrame::pointer
	AccountTypeLimitsHelper::storeLoad(LedgerKey const &key, Database &db)
	{
		auto const &accountTypeLimits = key.accountTypeLimits();
		return loadLimits(accountTypeLimits.accountType, db);
	}

	EntryFrame::pointer
	AccountTypeLimitsHelper::fromXDR(LedgerEntry const &from)
	{
		return std::make_shared<AccountTypeLimitsFrame>(from);
	}

	uint64_t
	AccountTypeLimitsHelper::countObjects(soci::session& sess)
	{
		uint64_t count = 0;
		sess << "SELECT COUNT(*) FROM account_type_limits;", into(count);
		return count;
	}

	AccountTypeLimitsFrame::pointer 
	AccountTypeLimitsHelper::loadLimits(AccountType accountType, Database& db, LedgerDelta* delta)
	{
		LedgerKey key;
		key.type(LedgerEntryType::ACCOUNT_TYPE_LIMITS);
		key.accountTypeLimits().accountType = accountType;
		if (cachedEntryExists(key, db))
		{
			auto p = getCachedEntry(key, db);
			return p ? make_shared<AccountTypeLimitsFrame>(*p) : nullptr;
		}

		LedgerEntry le;
		le.data.type(LedgerEntryType::ACCOUNT_TYPE_LIMITS);
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