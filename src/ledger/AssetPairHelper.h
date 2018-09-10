#pragma once

// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ledger/EntryHelperLegacy.h"
#include "ledger/LedgerManager.h"
#include <functional>
#include <unordered_map>
#include "AssetPairFrame.h"

namespace soci
{
	class session;
}

namespace stellar
{
	class StatementContext;

	class AssetPairHelper : public EntryHelperLegacy {
	public:
		static AssetPairHelper* Instance() {
			static AssetPairHelper singleton;
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

		bool exists(Database& db, AssetCode base, AssetCode quote);

		AssetPairFrame::pointer loadAssetPair(AssetCode base, AssetCode quote,
			Database& db, LedgerDelta *delta = nullptr);

		AssetPairFrame::pointer mustLoadAssetPair(AssetCode base, AssetCode quote,
			Database& db, LedgerDelta *delta = nullptr) {
			auto result = loadAssetPair(base, quote, db, delta);
			if (!result) {
				CLOG(ERROR, "EntryFrame") << "Unexpected db state. Expected asset pair to exists. Base " << std::string(base)
					<< " Quote " << std::string(quote);
				throw std::runtime_error("Unexpected db state. Expected asset pair to exist");
			}
			return result;
		}

                // tryLoadAssetPairForAssets - tries to load code1/code2 asset pair, if not found loads code2/code1 
                AssetPairFrame::pointer tryLoadAssetPairForAssets(AssetCode code1, AssetCode code2, Database& db, LedgerDelta * delta = nullptr);

		void loadAssetPairsByQuote(AssetCode quoteAsset, Database& db, std::vector<AssetPairFrame::pointer>& retAssetPairs);

	private:
		AssetPairHelper() { ; }
		~AssetPairHelper() { ; }

		AssetPairHelper(AssetPairHelper const&) = delete;
		AssetPairHelper& operator=(AssetPairHelper const&) = delete;

		void loadAssetPairs(StatementContext& prep,
			std::function<void(LedgerEntry const&)> AssetPairProcessor);

		void storeUpdateHelper(LedgerDelta& delta, Database& db, bool insert, LedgerEntry const& entry);

	};

}
