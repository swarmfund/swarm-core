#pragma once

// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ledger/EntryHelperLegacy.h"
#include <functional>
#include <unordered_map>
#include "AccountTypeLimitsFrame.h"

namespace soci
{
	class session;
}

namespace stellar
{
	class StatementContext;

	class AccountTypeLimitsHelper : public EntryHelperLegacy {
	public:

		static AccountTypeLimitsHelper *Instance() {
			static AccountTypeLimitsHelper singleton;
			return &singleton;
		}

		void dropAll(Database& db) override;

		void storeAdd(LedgerDelta& delta, Database& db, LedgerEntry const& entry) override;
		void storeChange(LedgerDelta& delta, Database& db, LedgerEntry const& entry) override;
		void storeDelete(LedgerDelta &delta, Database &db, LedgerKey const &key) override;
		bool exists(Database &db, LedgerKey const &key) override;
		LedgerKey getLedgerKey(LedgerEntry const &from) override;
		EntryFrame::pointer storeLoad(LedgerKey const &key, Database &db) override;
		EntryFrame::pointer fromXDR(LedgerEntry const &from) override;
		uint64_t countObjects(soci::session& sess) override;

		AccountTypeLimitsFrame::pointer
			loadLimits(AccountType accountType, Database &db, LedgerDelta *delta = nullptr);

	private:
		AccountTypeLimitsHelper() { ; }
		~AccountTypeLimitsHelper() { ; }

		AccountTypeLimitsHelper(AccountTypeLimitsHelper const&) = delete;
		AccountTypeLimitsHelper& operator= (AccountTypeLimitsHelper const&) = delete;

		void storeUpdateHelper(LedgerDelta& delta, Database& db, bool insert, LedgerEntry const& entry);
	};
}