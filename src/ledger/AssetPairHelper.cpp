// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "AssetPairHelper.h"
#include "crypto/SecretKey.h"
#include "crypto/Hex.h"
#include "database/Database.h"
#include "LedgerDelta.h"
#include "ledger/LedgerManager.h"
#include "util/basen.h"
#include "util/types.h"
#include "lib/util/format.h"
#include <algorithm>

using namespace soci;
using namespace std;

namespace stellar
{
	using xdr::operator<;

	static const char* assetPairColumnSelector =
		"SELECT base, quote, current_price, physical_price, physical_price_correction, max_price_step, policies, "
		"lastmodified, version "
		"FROM asset_pair";

	void
	AssetPairHelper::dropAll(Database& db)
	{
		db.getSession() << "DROP TABLE IF EXISTS asset_pair;";
		db.getSession() << "CREATE TABLE asset_pair"
			"("
			"base                      VARCHAR(16)       NOT NULL,"
			"quote                     VARCHAR(16)       NOT NULL,"
			"current_price             BIGINT            NOT NULL CHECK(current_price >= 0), "
			"physical_price            BIGINT            NOT NULL CHECK(physical_price >= 0), "
			"physical_price_correction BIGINT            NOT NULL CHECK(physical_price_correction >= 0), "
			"max_price_step            BIGINT            NOT NULL CHECK(max_price_step >= 0), "
			"policies                  INT               NOT NULL, "
			"lastmodified              INT               NOT NULL, "
			"version				   INT				 NOT NULL DEFAULT 0, "
			"PRIMARY KEY (base, quote)"
			");";
	}

	void
	AssetPairHelper::storeUpdateHelper(LedgerDelta& delta, Database& db, bool insert, LedgerEntry const& entry)
	{
		auto assetPairFrame = make_shared<AssetPairFrame>(entry);
		auto assetPairEntry = assetPairFrame->getAssetPair();

		assetPairFrame->touch(delta);

		bool isValid = assetPairFrame->isValid();
		if (!isValid)
		{
			throw std::runtime_error("Invalid asset pair");
		}

		auto key = assetPairFrame->getKey();
		flushCachedEntry(key, db);
		string sql;

		if (insert)
		{
			sql = "INSERT INTO asset_pair (base, quote, current_price, physical_price, physical_price_correction, "
				"max_price_step, policies, lastmodified, version) "
				"VALUES (:b, :q, :cp, :pp, :ppc, :mps, :p, :lm, :v)";
		}
		else
		{
			sql = "UPDATE asset_pair "
				"SET 	  current_price=:cp, physical_price=:pp, physical_price_correction=:ppc, max_price_step=:mps, policies=:p, "
				"       lastmodified=:lm, version=:v "
				"WHERE  base = :b AND quote=:q";
		}

		auto prep = db.getPreparedStatement(sql);
		auto& st = prep.statement();

		string base = assetPairFrame->getBaseAsset();
		string quote = assetPairFrame->getQuoteAsset();
		int32_t assetPairVersion = static_cast<int32_t >(assetPairFrame->getAssetPair().ext.v());

		st.exchange(use(base, "b"));
		st.exchange(use(quote, "q"));
		st.exchange(use(assetPairEntry.currentPrice, "cp"));
		st.exchange(use(assetPairEntry.physicalPrice, "pp"));
		st.exchange(use(assetPairEntry.physicalPriceCorrection, "ppc"));
		st.exchange(use(assetPairEntry.maxPriceStep, "mps"));
		st.exchange(use(assetPairEntry.policies, "p"));
		st.exchange(use(assetPairFrame->mEntry.lastModifiedLedgerSeq, "lm"));
		st.exchange(use(assetPairVersion, "v"));
		st.define_and_bind();

		auto timer =
			insert ? db.getInsertTimer("assetPair") : db.getUpdateTimer("assetPair");
		st.execute(true);

		if (st.get_affected_rows() != 1)
		{
			throw std::runtime_error("could not update SQL");
		}

		if (insert)
		{
			delta.addEntry(*assetPairFrame);
		}
		else
		{
			delta.modEntry(*assetPairFrame);
		}

	}

	void
	AssetPairHelper::storeAdd(LedgerDelta& delta, Database& db, LedgerEntry const& entry)
	{
		storeUpdateHelper(delta, db, true, entry);
	}

	void
	AssetPairHelper::storeChange(LedgerDelta& delta, Database& db, LedgerEntry const& entry)
	{
		storeUpdateHelper(delta, db, false, entry);
	}

	void
	AssetPairHelper::storeDelete(LedgerDelta& delta, Database& db, LedgerKey const& key)
	{
		flushCachedEntry(key, db);
		auto timer = db.getDeleteTimer("AssetPair");
		auto prep = db.getPreparedStatement("DELETE FROM asset_pair WHERE base=:base AND quote=:quote");
		auto& st = prep.statement();
		string base = key.assetPair().base;
		string quote = key.assetPair().quote;
		st.exchange(use(base));
		st.exchange(use(quote));
		st.define_and_bind();
		st.execute(true);
		delta.deleteEntry(key);
	}

	bool
	AssetPairHelper::exists(Database& db, LedgerKey const& key)
	{
		return exists(db, key.assetPair().base, key.assetPair().quote);
	}

	bool
	AssetPairHelper::exists(Database& db, AssetCode base, AssetCode quote)
	{
		int exists = 0;
		auto timer = db.getSelectTimer("assetPair-exists");
		auto prep =
			db.getPreparedStatement("SELECT EXISTS (SELECT NULL FROM asset_pair WHERE base=:base AND quote=:quote)");
		auto& st = prep.statement();
		string baseCode = base;
		string quoteCode = quote;
		st.exchange(use(baseCode));
		st.exchange(use(quoteCode));
		st.exchange(into(exists));
		st.define_and_bind();
		st.execute(true);

		return exists != 0;
	}

	LedgerKey
	AssetPairHelper::getLedgerKey(LedgerEntry const& from)
	{
		LedgerKey ledgerKey;
		ledgerKey.type(from.data.type());
		ledgerKey.assetPair().base = from.data.assetPair().base;
		ledgerKey.assetPair().quote = from.data.assetPair().quote;
		return ledgerKey;
	}

	EntryFrame::pointer
	AssetPairHelper::storeLoad(LedgerKey const& key, Database& db)
	{
		if (cachedEntryExists(key, db)) {
			auto p = getCachedEntry(key, db);
			return p ? std::make_shared<AssetPairFrame>(*p) : nullptr;
		}

		string baseCode = key.assetPair().base;
		string quoteCode = key.assetPair().quote;
		std::string sql = assetPairColumnSelector;
		sql += " WHERE base = :base AND quote = :quote";
		auto prep = db.getPreparedStatement(sql);
		auto& st = prep.statement();
		st.exchange(use(baseCode));
		st.exchange(use(quoteCode));

		auto timer = db.getSelectTimer("assetPair");
		AssetPairFrame::pointer retAssetPair;
		loadAssetPairs(prep, [&retAssetPair](LedgerEntry const& assetPair) {
			retAssetPair = make_shared<AssetPairFrame>(assetPair);
		});

		if (!retAssetPair) {
			putCachedEntry(key, nullptr, db);
			return nullptr;
		}
		auto pEntry = std::make_shared<LedgerEntry const>(retAssetPair->mEntry);
		putCachedEntry(key, pEntry, db);
		return retAssetPair;
	}

	EntryFrame::pointer
	AssetPairHelper::fromXDR(LedgerEntry const& from)
	{
		return std::make_shared<AssetPairFrame>(from);
	}

	uint64_t
	AssetPairHelper::countObjects(soci::session& sess)
	{
		uint64_t count = 0;
		sess << "SELECT COUNT(*) FROM asset_pair;", into(count);
		return count;
	}

	AssetPairFrame::pointer
	AssetPairHelper::loadAssetPair(AssetCode base, AssetCode quote, Database& db,
			LedgerDelta* delta)
	{
		LedgerKey key;
		key.type(LedgerEntryType::ASSET_PAIR);
		key.assetPair().base = base;
		key.assetPair().quote = quote;

		if (cachedEntryExists(key, db))
		{
			auto p = getCachedEntry(key, db);
			return p ? std::make_shared<AssetPairFrame>(*p) : nullptr;
		}

		string baseCode = base;
		string quoteCode = quote;
		std::string sql = assetPairColumnSelector;
		sql += " WHERE base = :base AND quote = :quote";
		auto prep = db.getPreparedStatement(sql);
		auto& st = prep.statement();
		st.exchange(use(baseCode));
		st.exchange(use(quoteCode));

		auto timer = db.getSelectTimer("assetPair");
		AssetPairFrame::pointer retAssetPair;
		loadAssetPairs(prep, [&retAssetPair](LedgerEntry const& assetPair)
		{
			retAssetPair = make_shared<AssetPairFrame>(assetPair);
		});

		if (!retAssetPair)
		{
			putCachedEntry(key, nullptr, db);
			return nullptr;
		}

		if (delta)
		{
			delta->recordEntry(*retAssetPair);
		}
		auto pEntry = std::make_shared<LedgerEntry const>(retAssetPair->mEntry);
		putCachedEntry(key, pEntry, db);
		return retAssetPair;
	}

	void AssetPairHelper::loadAssetPairsByQuote(AssetCode quoteAsset, Database& db, std::vector<AssetPairFrame::pointer>& retAssetPairs)
	{

		string quoteCode = quoteAsset;
		std::string sql = assetPairColumnSelector;
		sql += " WHERE quote = :quote";
		auto prep = db.getPreparedStatement(sql);
		auto& st = prep.statement();
		st.exchange(use(quoteCode));

		auto timer = db.getSelectTimer("assert-pair-by-quote");
		loadAssetPairs(prep, [&retAssetPairs](LedgerEntry const& of)
		{
			retAssetPairs.emplace_back(make_shared<AssetPairFrame>(of));
		});
	}

	void
	AssetPairHelper::loadAssetPairs(StatementContext& prep,
			std::function<void(LedgerEntry const&)> assetPairProcessor)
	{
		LedgerEntry le;
		le.data.type(LedgerEntryType::ASSET_PAIR);
		AssetPairEntry& oe = le.data.assetPair();
		string baseCode, quoteCode;
		int32_t assetPairVersion = 0;

		statement& st = prep.statement();
		st.exchange(into(baseCode));
		st.exchange(into(quoteCode));
		st.exchange(into(oe.currentPrice));
		st.exchange(into(oe.physicalPrice));
		st.exchange(into(oe.physicalPriceCorrection));
		st.exchange(into(oe.maxPriceStep));
		st.exchange(into(oe.policies));
		st.exchange(into(le.lastModifiedLedgerSeq));
		st.exchange(into(assetPairVersion));
		st.define_and_bind();
		st.execute(true);
		while (st.got_data())
		{
			oe.base = baseCode;
			oe.quote = quoteCode;
			oe.ext.v((LedgerVersion)assetPairVersion);

			bool isValid = AssetPairFrame::isValid(oe);
			if (!isValid)
			{
				throw std::runtime_error("Invalid asset pair");
			}

			assetPairProcessor(le);
			st.fetch();
		}
	}

}