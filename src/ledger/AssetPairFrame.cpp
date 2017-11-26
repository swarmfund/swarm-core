// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "AssetPairFrame.h"
#include "AssetFrame.h"
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

const char* AssetPairFrame::kSQLCreateStatement1 =
    "CREATE TABLE asset_pair"
    "("
    "base                      VARCHAR(16)       NOT NULL,"
	"quote                     VARCHAR(16)       NOT NULL,"
	"current_price             BIGINT            NOT NULL CHECK(current_price >= 0), "
	"physical_price            BIGINT            NOT NULL CHECK(physical_price >= 0), "
	"physical_price_correction BIGINT            NOT NULL CHECK(physical_price_correction >= 0), "
	"max_price_step            BIGINT            NOT NULL CHECK(max_price_step >= 0), "
	"policies                  INT               NOT NULL, "
    "lastmodified              INT               NOT NULL, "
    "PRIMARY KEY (base, quote)"
    ");";
static const char* assetPairColumnSelector =
"SELECT base, quote, current_price, physical_price, physical_price_correction, max_price_step, policies, lastmodified "
"FROM asset_pair";

AssetPairFrame::AssetPairFrame() : EntryFrame(ASSET_PAIR), mAssetPair(mEntry.data.assetPair())
{
}

AssetPairFrame::AssetPairFrame(LedgerEntry const& from)
    : EntryFrame(from), mAssetPair(mEntry.data.assetPair())
{
}

AssetPairFrame::AssetPairFrame(AssetPairFrame const& from) : AssetPairFrame(from.mEntry)
{
}

AssetPairFrame& AssetPairFrame::operator=(AssetPairFrame const& other)
{
    if (&other != this)
    {
        mAssetPair = other.mAssetPair;
        mKey = other.mKey;
        mKeyCalculated = other.mKeyCalculated;
    }
    return *this;
}

AssetPairFrame::pointer AssetPairFrame::create(AssetCode base, AssetCode quote, int64_t currentPrice, int64_t physicalPrice,
	int64_t physicalPriceCorrection, int64_t maxPriceStep, int32_t policies)
{
	LedgerEntry le;
	le.data.type(ASSET_PAIR);
	AssetPairEntry& assetPair = le.data.assetPair();
	assetPair.base = base;
	assetPair.quote = quote;
	assetPair.currentPrice = currentPrice;
	assetPair.physicalPrice = physicalPrice;
	assetPair.physicalPriceCorrection = physicalPriceCorrection;
	assetPair.maxPriceStep = maxPriceStep;
	assetPair.policies = policies;
	return make_shared<AssetPairFrame>(le);
}

bool
AssetPairFrame::isValid(AssetPairEntry const& oe)
{
	return AssetFrame::isAssetCodeValid(oe.base) && AssetFrame::isAssetCodeValid(oe.quote) && oe.currentPrice >= 0 && oe.maxPriceStep >= 0
		&& oe.physicalPrice >= 0 && oe.physicalPriceCorrection >= 0;
}

bool
AssetPairFrame::isValid() const
{
    return isValid(mAssetPair);
}

bool AssetPairFrame::getPhysicalPriceWithCorrection(int64_t& result) const
{
	return bigDivide(result, mAssetPair.physicalPrice, mAssetPair.physicalPriceCorrection, 100 * ONE, ROUND_UP);
}

int64_t AssetPairFrame::getMinPriceInTermsOfCurrent() const
{
	int64_t minPriceInTermsOfCurrent = 0;
	if (checkPolicy(ASSET_PAIR_CURRENT_PRICE_RESTRICTION))
	{
		int64_t maxPrice = 0;
		if (!getCurrentPriceCoridor(minPriceInTermsOfCurrent, maxPrice))
		{
			throw std::runtime_error("Current price coridor overflow");
		}
	}

	return minPriceInTermsOfCurrent;
}

int64_t AssetPairFrame::getMinPriceInTermsOfPhysical() const
{
	int64_t minPriceInTermsOfPhysical = 0;
	if (checkPolicy(ASSET_PAIR_PHYSICAL_PRICE_RESTRICTION))
	{
		if (!getPhysicalPriceWithCorrection(minPriceInTermsOfPhysical))
		{
			throw std::runtime_error("Physical price calculation overflow");
		}
	}

	return minPriceInTermsOfPhysical;
}

int64_t AssetPairFrame::getMinAllowedPrice() const
{
	return  getMinPriceInTermsOfPhysical();
}

bool AssetPairFrame::getCurrentPriceCoridor(int64_t& min, int64_t& max) const
{
	int64_t minInPercent = 100 * ONE - mAssetPair.maxPriceStep;
	if (minInPercent < 0)
		return false;
	int64_t maxInPercent = 100 * ONE + mAssetPair.maxPriceStep;
	if (maxInPercent < 0)
		return false;
	return bigDivide(min, mAssetPair.currentPrice, minInPercent, 100 * ONE, ROUND_UP) 
		&& bigDivide(max, mAssetPair.currentPrice, maxInPercent, 100 * ONE, ROUND_DOWN);
}

void AssetPairFrame::loadAssetPairsByQuote(AssetCode quoteAsset, Database& db, std::vector<AssetPairFrame::pointer>& retAssetPairs)
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

AssetPairFrame::pointer
AssetPairFrame::loadAssetPair(AssetCode base, AssetCode quote, Database& db,
                      LedgerDelta* delta)
{
	LedgerKey key;
	key.type(ASSET_PAIR);
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
	retAssetPair->putCachedEntry(db);
	return retAssetPair;
}

void
AssetPairFrame::loadAssetPairs(StatementContext& prep,
                       std::function<void(LedgerEntry const&)> assetPairProcessor)
{
    LedgerEntry le;
    le.data.type(ASSET_PAIR);
    AssetPairEntry& oe = le.data.assetPair();
	string baseCode, quoteCode;

    statement& st = prep.statement();
    st.exchange(into(baseCode));
    st.exchange(into(quoteCode));
	st.exchange(into(oe.currentPrice));
	st.exchange(into(oe.physicalPrice));
	st.exchange(into(oe.physicalPriceCorrection));
	st.exchange(into(oe.maxPriceStep));
	st.exchange(into(oe.policies));
    st.exchange(into(le.lastModifiedLedgerSeq));
    st.define_and_bind();
    st.execute(true);
    while (st.got_data())
    {
        oe.base = baseCode;
		oe.quote = quoteCode;
        if (!isValid(oe))
        {
            throw std::runtime_error("Invalid asset pair");
        }

        assetPairProcessor(le);
        st.fetch();
    }
}


uint64_t
AssetPairFrame::countObjects(soci::session& sess)
{
    uint64_t count = 0;
    sess << "SELECT COUNT(*) FROM asset_pair;", into(count);
    return count;
}

void
AssetPairFrame::storeDelete(LedgerDelta& delta, Database& db) const
{
    storeDelete(delta, db, getKey());
}

void
AssetPairFrame::storeDelete(LedgerDelta& delta, Database& db, LedgerKey const& key)
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

void
AssetPairFrame::storeChange(LedgerDelta& delta, Database& db)
{
    storeUpdateHelper(delta, db, false);
}

void
AssetPairFrame::storeAdd(LedgerDelta& delta, Database& db)
{
    storeUpdateHelper(delta, db, true);
}

void
AssetPairFrame::storeUpdateHelper(LedgerDelta& delta, Database& db, bool insert)
{
	touch(delta);

	if (!isValid())
	{
		throw std::runtime_error("Invalid asset pair");
	}

	flushCachedEntry(db);
    string sql;

    if (insert)
    {
		sql = "INSERT INTO asset_pair (base, quote, current_price, physical_price, physical_price_correction, max_price_step, policies, lastmodified)"
			"VALUES (:b, :q, :cp, :pp, :ppc, :mps, :p, :lm)";
    }
    else
    {
        sql = "UPDATE asset_pair SET current_price=:cp, physical_price=:pp, physical_price_correction=:ppc, max_price_step=:mps, policies =:p, "
			"lastmodified=:lm "
            "WHERE base = :b AND quote=:q";
    }

    auto prep = db.getPreparedStatement(sql);
    auto& st = prep.statement();

	string base = mAssetPair.base;
	string quote = mAssetPair.quote;

    st.exchange(use(base, "b"));
	st.exchange(use(quote, "q"));
	st.exchange(use(mAssetPair.currentPrice, "cp"));
	st.exchange(use(mAssetPair.physicalPrice, "pp"));
	st.exchange(use(mAssetPair.physicalPriceCorrection, "ppc"));
	st.exchange(use(mAssetPair.maxPriceStep, "mps"));
	st.exchange(use(mAssetPair.policies, "p"));
	st.exchange(use(mEntry.lastModifiedLedgerSeq, "lm"));
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
        delta.addEntry(*this);
    }
    else
    {
        delta.modEntry(*this);
    }

}

bool
AssetPairFrame::exists(Database& db, LedgerKey const& key)
{
	return exists(db, key.assetPair().base, key.assetPair().quote);
}

bool
AssetPairFrame::exists(Database& db, AssetCode base, AssetCode quote)
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

void
AssetPairFrame::dropAll(Database& db)
{
    db.getSession() << "DROP TABLE IF EXISTS asset_pair;";
    db.getSession() << kSQLCreateStatement1;
}
}

