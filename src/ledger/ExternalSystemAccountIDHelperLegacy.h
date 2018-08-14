#pragma once

// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ledger/EntryHelperLegacy.h"
#include "ledger/ExternalSystemAccountID.h"

namespace soci
{
	class session;
}

namespace stellar
{
	class StatementContext;

	class ExternalSystemAccountIDHelperLegacy : public EntryHelperLegacy {
	public:
		ExternalSystemAccountIDHelperLegacy() = default;

		static ExternalSystemAccountIDHelperLegacy *Instance() {
			static ExternalSystemAccountIDHelperLegacy singleton;
			return&singleton;
		}

		void dropAll(Database& db) override;
		void storeAdd(LedgerDelta& delta, Database& db, LedgerEntry const& entry) override;
		void storeChange(LedgerDelta& delta, Database& db, LedgerEntry const& entry) override;
		void storeDelete(LedgerDelta& delta, Database& db, LedgerKey const& key) override;
		bool exists(Database& db, LedgerKey const& key) override;
		LedgerKey getLedgerKey(LedgerEntry const& from) override;
		EntryFrame::pointer storeLoad(LedgerKey const& key, Database& db) override;
		EntryFrame::pointer fromXDR(LedgerEntry const& from) override;
		uint64_t countObjects(soci::session& sess) override;

		bool exists(Database& db, AccountID accountID, int32 externalSystemType);

        std::vector<ExternalSystemAccountIDFrame::pointer> loadAll(Database& db);

		// load - loads external system account ID by accountID and externalSystemType. If not found returns nullptr.
		ExternalSystemAccountIDFrame::pointer
			load(const AccountID accountID, const int32 externalSystemType, Database& db, LedgerDelta* delta = nullptr);

	private:

		ExternalSystemAccountIDHelperLegacy(ExternalSystemAccountIDHelperLegacy const&) = delete;
		ExternalSystemAccountIDHelperLegacy& operator=(ExternalSystemAccountIDHelperLegacy const&) = delete;

		static const char* select;

		void storeUpdateHelper(LedgerDelta& delta, Database& db, bool insert, LedgerEntry const& entry);
		void load(StatementContext& prep, std::function<void(LedgerEntry const&)> processor);

	};

}