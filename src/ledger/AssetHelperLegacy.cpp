#include "AssetHelperLegacy.h"
#include "LedgerDelta.h"
#include "xdrpp/printer.h"

using namespace soci;
using namespace std;

namespace stellar
{
using xdr::operator<;

static const char* assetColumnSelector =
    "SELECT code, owner, preissued_asset_signer, details, max_issuance_amount, available_for_issueance,"
    " issued, pending_issuance, policies, lastmodified, version FROM asset";

void AssetHelperLegacy::dropAll(Database& db)
{
    db.getSession() << "DROP TABLE IF EXISTS asset;";
    db.getSession() << "CREATE TABLE asset"
        "("
        "code                    VARCHAR(16)   NOT NULL,"
        "owner                   VARCHAR(56)   NOT NULL,"
        "preissued_asset_signer  VARCHAR(56)   NOT NULL,"
        "details                 TEXT          NOT NULL,"
        "max_issuance_amount     NUMERIC(20,0) NOT NULL CHECK (max_issuance_amount >= 0),"
        "available_for_issueance NUMERIC(20,0) NOT NULL CHECK (available_for_issueance >= 0),"
        "issued                  NUMERIC(20,0) NOT NULL CHECK (issued >= 0),"
        "pending_issuance         NUMERIC(20,0) NOT NULL CHECK (issued >= 0),"
        "policies                INT           NOT NULL, "
        "lastmodified            INT           NOT NULL, "
        "version                 INT           NOT NULL, "
        "PRIMARY KEY (code)"
        ");";
}

void AssetHelperLegacy::storeUpdateHelper(LedgerDelta& delta, Database& db,
                                    const bool insert, LedgerEntry const& entry)
{
    auto assetFrame = make_shared<AssetFrame>(entry);
    auto assetEntry = assetFrame->getAsset();

    assetFrame->touch(delta);

    assetFrame->ensureValid();

    const auto key = assetFrame->getKey();
    flushCachedEntry(key, db);

    auto assetVersion = static_cast<int32_t>(assetEntry.ext.v());

    string sql;

    if (insert)
    {
        sql =
            "INSERT INTO asset (code, owner, preissued_asset_signer, details, max_issuance_amount,"
            "available_for_issueance, issued, pending_issuance, policies, lastmodified, version) "
            "VALUES (:code, :owner, :preissued_asset_signer, :details, :max_issuance_amount, "
            ":available_for_issueance, :issued, :pending_issuance, :policies, :lm, :v)";
    }
    else
    {
        sql =
            "UPDATE asset SET owner = :owner, preissued_asset_signer = :preissued_asset_signer, details = :details,"
            " max_issuance_amount = :max_issuance_amount,"
            "available_for_issueance = :available_for_issueance, issued = :issued, pending_issuance = :pending_issuance, policies = :policies, lastmodified = :lm, version = :v "
            "WHERE code = :code";
    }

    auto prep = db.getPreparedStatement(sql);
    auto& st = prep.statement();

    st.exchange(use(assetEntry.code, "code"));
    auto owner = PubKeyUtils::toStrKey(assetEntry.owner);
    st.exchange(use(owner, "owner"));
    auto preissuedAssetSigner = PubKeyUtils::toStrKey(assetEntry.preissuedAssetSigner);
    st.exchange(use(preissuedAssetSigner, "preissued_asset_signer"));
    st.exchange(use(assetEntry.details, "details"));
    st.exchange(use(assetEntry.maxIssuanceAmount, "max_issuance_amount"));
    st.exchange(use(assetEntry.availableForIssueance,
                    "available_for_issueance"));
    st.exchange(use(assetEntry.issued, "issued"));
    st.exchange(use(assetEntry.pendingIssuance, "pending_issuance"));
    st.exchange(use(assetEntry.policies, "policies"));
    st.exchange(use(assetFrame->mEntry.lastModifiedLedgerSeq, "lm"));
    st.exchange(use(assetVersion, "v"));
    st.define_and_bind();

    auto timer = insert
                     ? db.getInsertTimer("asset")
                     : db.getUpdateTimer("asset");

    st.execute(true);
    if (st.get_affected_rows() != 1)
    {
        throw runtime_error("could not update SQL");
    }

    if (insert)
    {
        delta.addEntry(*assetFrame);
    }
    else
    {
        delta.modEntry(*assetFrame);
    }
}

void AssetHelperLegacy::storeAdd(LedgerDelta& delta, Database& db,
                           LedgerEntry const& entry)
{
    storeUpdateHelper(delta, db, true, entry);
}

void AssetHelperLegacy::storeChange(LedgerDelta& delta, Database& db,
                              LedgerEntry const& entry)
{
    storeUpdateHelper(delta, db, false, entry);
}

void AssetHelperLegacy::storeDelete(LedgerDelta& delta, Database& db,
                              LedgerKey const& key)
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

bool AssetHelperLegacy::exists(Database& db, LedgerKey const& key)
{
    return AssetHelperLegacy::exists(db, key.asset().code);
}

bool AssetHelperLegacy::exists(Database& db, const AssetCode code)
{
    int exists = 0;
    auto timer = db.getSelectTimer("asset-exists");
    std::string assetCode = code;
    auto prep =
        db.
        getPreparedStatement("SELECT EXISTS (SELECT NULL FROM asset WHERE code=:code)");
    auto& st = prep.statement();
    st.exchange(use(assetCode));
    st.exchange(into(exists));
    st.define_and_bind();
    st.execute(true);

    return exists != 0;
}

LedgerKey AssetHelperLegacy::getLedgerKey(LedgerEntry const& from)
{
    LedgerKey ledgerKey;
    ledgerKey.type(from.data.type());
    ledgerKey.asset().code = from.data.asset().code;
    return ledgerKey;
}

EntryFrame::pointer AssetHelperLegacy::storeLoad(LedgerKey const& key, Database& db)
{
    return loadAsset(key.asset().code, db);
}

EntryFrame::pointer AssetHelperLegacy::fromXDR(LedgerEntry const& from)
{
    return std::make_shared<AssetFrame>(from);
}

uint64_t AssetHelperLegacy::countObjects(soci::session& sess)
{
    uint64_t count = 0;
    sess << "SELECT COUNT(*) FROM asset;", into(count);
    return count;
}

AssetFrame::pointer
AssetHelperLegacy::loadAsset(AssetCode code, Database& db,
                       LedgerDelta* delta)
{
    LedgerKey key;
    key.type(LedgerEntryType::ASSET);
    key.asset().code = code;
    if (cachedEntryExists(key, db))
    {
        // TODO: ask Dima about it
        auto p = getCachedEntry(key, db);
        auto assetFrame = p ? std::make_shared<AssetFrame>(*p) : nullptr;
        if (!!delta && !!assetFrame)
        {
            delta->recordEntry(*assetFrame);
        }
    }

    AssetFrame::pointer retAsset;
    string assetCode = code;
    string sql = assetColumnSelector;
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

    auto pEntry = std::make_shared<LedgerEntry const>(retAsset->mEntry);
    putCachedEntry(key, pEntry, db);
    return retAsset;
}

AssetFrame::pointer AssetHelperLegacy::mustLoadAsset(AssetCode code, Database& db,
    LedgerDelta* delta)
{
    auto result = loadAsset(code, db, delta);
    if (!!result)
    {
        return result;
    }

    CLOG(ERROR, Logging::ENTRY_LOGGER) << "Unexpected state: expected asset to exist: " << code;
    throw runtime_error("Expected asset to exist");
}

AssetFrame::pointer AssetHelperLegacy::loadAsset(AssetCode code,
                                           AccountID const& owner, Database& db,
                                           LedgerDelta* delta)
{
    auto assetFrame = loadAsset(code, db, delta);
    if (!assetFrame)
        return nullptr;

    if (assetFrame->getAsset().owner == owner)
    {
        return assetFrame;
    }

    return nullptr;
}

AssetFrame::pointer AssetHelperLegacy::loadStatsAsset(Database& db)
{
    uint32 statsAssetPolicy = static_cast<uint32>(AssetPolicy::STATS_QUOTE_ASSET
    );

    AssetFrame::pointer retAsset;
    std::string sql = assetColumnSelector;
    sql += " WHERE policies & :sp = :sp";
    auto prep = db.getPreparedStatement(sql);
    auto& st = prep.statement();
    st.exchange(use(statsAssetPolicy, "sp"));

    auto timer = db.getSelectTimer("asset");
    loadAssets(prep, [&retAsset](LedgerEntry const& asset)
    {
        retAsset = make_shared<AssetFrame>(asset);
    });
    return retAsset;
}

void AssetHelperLegacy::loadAssets(std::vector<AssetFrame::pointer>& retAssets,
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

void AssetHelperLegacy::loadBaseAssets(std::vector<AssetFrame::pointer>& retAssets,
                                 Database& db)
{
    std::string sql = assetColumnSelector;
    sql += " WHERE policies & :bp = :bp "
        " ORDER BY code ";
    uint32 baseAssetPolicy = static_cast<uint32>(AssetPolicy::BASE_ASSET);
    auto prep = db.getPreparedStatement(sql);
    prep.statement().exchange(use(baseAssetPolicy, "bp"));

    auto timer = db.getSelectTimer("asset");
    loadAssets(prep, [&retAssets](LedgerEntry const& asset)
    {
        retAssets.push_back(make_shared<AssetFrame>(asset));
    });
}

void AssetHelperLegacy::loadAssets(StatementContext& prep,
                             std::function<void(LedgerEntry const&)>
                             assetProcessor)
{
    LedgerEntry le;
    le.data.type(LedgerEntryType::ASSET);
    AssetEntry& oe = le.data.asset();
    int32_t assetVersion;

    statement& st = prep.statement();
    st.exchange(into(oe.code));
    st.exchange(into(oe.owner));
    st.exchange(into(oe.preissuedAssetSigner));
    st.exchange(into(oe.details));
    st.exchange(into(oe.maxIssuanceAmount));
    st.exchange(into(oe.availableForIssueance));
    st.exchange(into(oe.issued));
    st.exchange(into(oe.pendingIssuance));
    st.exchange(into(oe.policies));
    st.exchange(into(le.lastModifiedLedgerSeq));
    st.exchange(into(assetVersion));
    st.define_and_bind();
    st.execute(true);
    while (st.got_data())
    {
        oe.ext.v(static_cast<LedgerVersion>(assetVersion));

        AssetFrame::ensureValid(oe);

        assetProcessor(le);
        st.fetch();
    }
}
}
