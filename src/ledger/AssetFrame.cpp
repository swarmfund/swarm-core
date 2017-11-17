// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "AssetFrame.h"
#include "crypto/SecretKey.h"
#include "database/Database.h"
#include "LedgerDelta.h"
#include "ledger/LedgerManager.h"
#include "util/types.h"
#include <util/basen.h>
#include "util/format.h"
#include "crypto/Hex.h"

using namespace soci;
using namespace std;

namespace stellar
{
using xdr::operator<;

const char* AssetFrame::kSQLCreateStatement1 =
    "CREATE TABLE asset"
    "("
    "code         VARCHAR(16)  NOT NULL,"
    "policies     INT          NOT NULL, "
    "lastmodified INT          NOT NULL, "
	"version      INT          NOT NULL     DEFAULT 0, "
    "PRIMARY KEY (code)"
    ");";
static const char* assetColumnSelector =
"SELECT code, policies, lastmodified, version "
"FROM asset";

AssetFrame::AssetFrame() : EntryFrame(ASSET), mAsset(mEntry.data.asset())
{
}

AssetFrame::AssetFrame(LedgerEntry const& from)
    : EntryFrame(from), mAsset(mEntry.data.asset())
{
}

AssetFrame::AssetFrame(AssetFrame const& from) : AssetFrame(from.mEntry)
{
}

AssetFrame& AssetFrame::operator=(AssetFrame const& other)
{
    if (&other != this)
    {
        mAsset = other.mAsset;
        mKey = other.mKey;
        mKeyCalculated = other.mKeyCalculated;
    }
    return *this;
}

AssetFrame::pointer AssetFrame::create(AssetCode code, int32_t policies)
{
	LedgerEntry le;
	le.data.type(ASSET);
	AssetEntry& baseAsset = le.data.asset();
	baseAsset.code = code;
	baseAsset.policies = policies;
	return std::make_shared<AssetFrame>(le);
}

bool
AssetFrame::isValid(AssetEntry const& oe)
{
	return isAssetValid(oe.code) && oe.policies >= 0;
}

bool
AssetFrame::isValid() const
{
    return isValid(mAsset);
}

void AssetFrame::loadAssets(std::vector<AssetFrame::pointer>& retAssets,
	Database& db)
{
	std::string sql = assetColumnSelector;
	auto prep = db.getPreparedStatement(sql);

	auto timer = db.getSelectTimer("asset");
	loadAssets(prep, [&retAssets](LedgerEntry const& asset)
	{
		retAssets.push_back(make_shared<AssetFrame>(asset));
	});
}


AssetFrame::pointer
AssetFrame::loadAsset(AssetCode code, Database& db,
                      LedgerDelta* delta)
{
	LedgerKey key;
	key.type(ASSET);
	key.asset().code = code;
	if (cachedEntryExists(key, db))
	{
		auto p = getCachedEntry(key, db);
		return p ? std::make_shared<AssetFrame>(*p) : nullptr;
	}

    AssetFrame::pointer retAsset;
    string assetCode = code;
    std::string sql = assetColumnSelector;
    sql += " WHERE code = :code";
    auto prep = db.getPreparedStatement(sql);
    auto& st = prep.statement();
    st.exchange(use(assetCode));

    auto timer = db.getSelectTimer("asset");
    loadAssets(prep, [&retAsset](LedgerEntry const& asset)
               {
                   retAsset = make_shared<AssetFrame>(asset);
               });

	if (!retAsset) 
	{
		putCachedEntry(key, nullptr, db);
		return nullptr;
	}

	if (delta)
	{
		delta->recordEntry(*retAsset);
	}
	retAsset->putCachedEntry(db);
	return retAsset;
}

void
AssetFrame::loadAssets(StatementContext& prep,
                       std::function<void(LedgerEntry const&)> assetProcessor)
{
    LedgerEntry le;
    le.data.type(ASSET);
    AssetEntry& oe = le.data.asset();
    string assetCode;
    int32_t assetVersion;

    statement& st = prep.statement();
    st.exchange(into(assetCode));
    st.exchange(into(oe.policies));
    st.exchange(into(le.lastModifiedLedgerSeq));
	st.exchange(into(assetVersion));
    st.define_and_bind();
    st.execute(true);
    while (st.got_data())
    {
        oe.ext.v((LedgerVersion)assetVersion);
        oe.code = assetCode;

        if (!isValid(oe))
        {
            throw std::runtime_error("Invalid asset");
        }

        assetProcessor(le);
        st.fetch();
    }
}


uint64_t
AssetFrame::countObjects(soci::session& sess)
{
    uint64_t count = 0;
    sess << "SELECT COUNT(*) FROM asset;", into(count);
    return count;
}

void
AssetFrame::storeDelete(LedgerDelta& delta, Database& db) const
{
    storeDelete(delta, db, getKey());
}

void
AssetFrame::storeDelete(LedgerDelta& delta, Database& db, LedgerKey const& key)
{
	flushCachedEntry(key, db);
    auto timer = db.getDeleteTimer("Asset");
    auto prep = db.getPreparedStatement("DELETE FROM asset WHERE code=:code");
    auto& st = prep.statement();
    string code = key.asset().code;
    st.exchange(use(code));
    st.define_and_bind();
    st.execute(true);
    delta.deleteEntry(key);
}

void
AssetFrame::storeChange(LedgerDelta& delta, Database& db)
{
    storeUpdateHelper(delta, db, false);
}

void
AssetFrame::storeAdd(LedgerDelta& delta, Database& db)
{
    storeUpdateHelper(delta, db, true);
}

void
AssetFrame::storeUpdateHelper(LedgerDelta& delta, Database& db, bool insert)
{
    touch(delta);

    if (!isValid())
    {
        throw std::runtime_error("Invalid Asset");
    }

	flushCachedEntry(db);

    string assetCode = mAsset.code;
    int32_t assetVersion = mAsset.ext.v();

    string sql;

    if (insert)
    {
		sql = "INSERT INTO asset (code, policies, lastmodified, version) "
              "VALUES (:code, :pol, :lm, :v)";
    }
    else
    {
        sql = "UPDATE asset SET policies=:pol, "
              "lastmodified=:lm, version=:v "
              "WHERE code = :code";
    }

    auto prep = db.getPreparedStatement(sql);
    auto& st = prep.statement();

    st.exchange(use(assetCode, "code"));
    st.exchange(use(mAsset.policies, "pol"));

	st.exchange(use(mEntry.lastModifiedLedgerSeq, "lm"));
	st.exchange(use(assetVersion, "v"));
    st.define_and_bind();

    auto timer =
        insert ? db.getInsertTimer("asset") : db.getUpdateTimer("asset");
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
AssetFrame::exists(Database& db, LedgerKey const& key)
{
	return AssetFrame::exists(db, key.asset().code);
}

bool
AssetFrame::exists(Database& db, AssetCode code)
{
	int exists = 0;
	auto timer = db.getSelectTimer("asset-exists");
    std::string assetCode = code;
	auto prep =
		db.getPreparedStatement("SELECT EXISTS (SELECT NULL FROM asset WHERE code=:code)");
	auto& st = prep.statement();
	st.exchange(use(assetCode));
	st.exchange(into(exists));
	st.define_and_bind();
	st.execute(true);

	return exists != 0;
}

void
AssetFrame::dropAll(Database& db)
{
    db.getSession() << "DROP TABLE IF EXISTS asset;";
    db.getSession() << kSQLCreateStatement1;
}
}

