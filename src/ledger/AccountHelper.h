#pragma once

// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ledger/EntryHelperLegacy.h"
#include <functional>
#include "map"
#include <unordered_map>
#include "xdr/Stellar-ledger-entries-account.h"
#include "AccountFrame.h"

namespace soci
{
	class session;
	namespace details
	{
		class prepare_temp_type;
	}
}

namespace stellar
{
	class LedgerManager;

	class AccountHelper : public EntryHelperLegacy {
	public:
		AccountHelper(AccountHelper const&) = delete;
		AccountHelper &operator=(AccountHelper const&) = delete;

		static AccountHelper* Instance() {
			static AccountHelper singleton;
			return &singleton;
		}
		void addKYCLevel(Database& db);
		void dropAll(Database& db) override;

		void storeAdd(LedgerDelta& delta, Database& db, LedgerEntry const& entry) override;
		void storeChange(LedgerDelta& delta, Database& db, LedgerEntry const& entry) override;
		void storeDelete(LedgerDelta& delta, Database& db, LedgerKey const& key) override;
		bool exists(Database& db, LedgerKey const& key) override;
		bool exists(AccountID const& accountID, Database& db);
		void ensureExists(AccountID const &accountID, Database &db);
		LedgerKey getLedgerKey(LedgerEntry const& from) override;
		EntryFrame::pointer storeLoad(LedgerKey const& key, Database& db) override;
		EntryFrame::pointer fromXDR(LedgerEntry const& from) override;
		uint64_t countObjects(soci::session& sess) override;

		AccountFrame::pointer loadAccount(AccountID const& accountID, Database& db, LedgerDelta* delta = nullptr);

		AccountFrame::pointer loadAccount(LedgerDelta& delta, AccountID const& accountID, Database& db);

		AccountFrame::pointer mustLoadAccount(AccountID const& accountID, Database& db);

		// loads all accounts from database and checks for consistency (slow!)
		std::unordered_map<AccountID, AccountFrame::pointer> checkDB(Database& db);

	private:
		AccountHelper() { ; }
		~AccountHelper() { ; }

		void storeUpdate(LedgerDelta& delta, Database& db, bool insert, LedgerEntry const& entry);

		// work with signers
		std::vector<Signer> loadSigners(Database& db, std::string const& actIDStrKey);
		void applySigners(Database& db, bool insert, LedgerDelta& delta, LedgerEntry const& entry);
		void deleteSigner(Database& db, std::string const& accountID, AccountID const& pubKey);
		void signerStoreChange(Database& db, LedgerDelta& delta, std::string const& accountID, std::vector<Signer>::iterator const& signer, bool insert);
	};


}