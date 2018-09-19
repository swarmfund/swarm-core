#include "AssetHelperImpl.h"
#include "ledger/LedgerDelta.h"
#include "ledger/StorageHelper.h"
#include <memory>
#include <xdrpp/marshal.h>
#include "util/basen.h"

using namespace soci;
using namespace std;

namespace stellar
{

using xdr::operator<;

AssetHelperImpl::AssetHelperImpl(StorageHelper& storageHelper)
        : mStorageHelper(storageHelper)
{
    mAssetColumnSelector = "SELECT code, owner, preissued_asset_signer, "
                           "details, max_issuance_amount, "
                           "available_for_issueance, issued, pending_issuance, "
                           "policies, lastmodified, version "
                           "FROM asset";
}

void
AssetHelperImpl::dropAll()
{
    Database& db = getDatabase();

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
           "pending_issuance        NUMERIC(20,0) NOT NULL CHECK (issued >= 0),"
           "policies                INT           NOT NULL, "
           "lastmodified            INT           NOT NULL, "
           "version                 INT           NOT NULL, "
           "PRIMARY KEY (code)"
           ");";
}

void
AssetHelperImpl::storeAdd(LedgerEntry const& entry)
{
    storeUpdateHelper(true, entry);
}

void
AssetHelperImpl::storeChange(LedgerEntry const& entry)
{
    storeUpdateHelper(false, entry);
}

void
AssetHelperImpl::storeDelete(LedgerKey const& key)
{
    flushCachedEntry(key);

    Database& db = getDatabase();
    auto timer = db.getDeleteTimer("delete-asset");
    auto prep = db.getPreparedStatement("DELETE FROM asset WHERE code=:code");
    auto& st = prep.statement();
    st.exchange(use(key.asset().code, "code"));
    st.define_and_bind();
    st.execute(true);
    mStorageHelper.getLedgerDelta().deleteEntry(key);
}

bool
AssetHelperImpl::exists(LedgerKey const& key)
{
    if (cachedEntryExists(key))
    {
        return true;
    }
    int exists = 0;

    Database& db = getDatabase();
    auto timer = db.getSelectTimer("asset-exists");
    auto prep = db.getPreparedStatement("SELECT EXISTS "
                                "(SELECT NULL FROM asset WHERE code=:code)");
    auto& st = prep.statement();
    st.exchange(use(key.asset().code, "code"));
    st.exchange(into(exists));
    st.define_and_bind();
    st.execute(true);

    return exists != 0;
}

void
AssetHelperImpl::storeUpdateHelper(bool insert, LedgerEntry const& entry)
{
    Database& db = getDatabase();
    LedgerDelta& delta = mStorageHelper.getLedgerDelta();

    auto assetFrame = make_shared<AssetFrame>(entry);
    auto assetEntry = assetFrame->getAsset();

    assetFrame->touch(delta);
    putCachedEntry(getLedgerKey(entry), make_shared<LedgerEntry>(entry));

    assetFrame->ensureValid();

    auto owner = PubKeyUtils::toStrKey(assetEntry.owner);
    auto signer = PubKeyUtils::toStrKey(assetEntry.preissuedAssetSigner);
    auto version = static_cast<int32_t>(assetEntry.ext.v());

    string sql;

    if (insert)
    {
        sql = "INSERT INTO asset (code, owner, preissued_asset_signer, details,"
              "                   max_issuance_amount, available_for_issueance,"
              "                   issued, pending_issuance, policies,"
              "                   lastmodified, version) "
              "VALUES (:code, :owner, :signer, :details, :max, :available, "
              "        :issued, :pending, :policies, :lm, :v)";
    }
    else
    {
        sql = "UPDATE asset SET owner = :owner, "
              "preissued_asset_signer = :signer, details = :details, "
              "max_issuance_amount = :max, "
              "available_for_issueance = :available, issued = :issued, "
              "pending_issuance = :pending, policies = :policies, "
              "lastmodified = :lm, version = :v "
              "WHERE code = :code";
    }

    auto prep = db.getPreparedStatement(sql);
    auto& st = prep.statement();

    st.exchange(use(assetEntry.code, "code"));
    st.exchange(use(owner, "owner"));
    st.exchange(use(signer, "signer"));
    st.exchange(use(assetEntry.details, "details"));
    st.exchange(use(assetEntry.maxIssuanceAmount, "max"));
    st.exchange(use(assetEntry.availableForIssueance, "available"));
    st.exchange(use(assetEntry.issued, "issued"));
    st.exchange(use(assetEntry.pendingIssuance, "pending"));
    st.exchange(use(assetEntry.policies, "policies"));
    st.exchange(use(assetFrame->mEntry.lastModifiedLedgerSeq, "lm"));
    st.exchange(use(version, "v"));
    st.define_and_bind();

    auto timer = insert ? db.getInsertTimer("insert-asset")
                        : db.getUpdateTimer("update-asset");

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

LedgerKey
AssetHelperImpl::getLedgerKey(LedgerEntry const& from)
{
    LedgerKey ledgerKey;
    ledgerKey.type(from.data.type());
    ledgerKey.asset().code = from.data.asset().code;
    return ledgerKey;
}

EntryFrame::pointer
AssetHelperImpl::storeLoad(LedgerKey const& key)
{
    return loadAsset(key.asset().code);
}

EntryFrame::pointer
AssetHelperImpl::fromXDR(LedgerEntry const& from)
{
    return make_shared<AssetFrame>(from);
}

uint64_t
AssetHelperImpl::countObjects()
{
    uint64_t count = 0;
    getDatabase().getSession() << "SELECT COUNT(*) FROM asset;", into(count);
    return count;
}

AssetFrame::pointer
AssetHelperImpl::loadAsset(AssetCode assetCode)
{
    LedgerKey key;
    key.type(LedgerEntryType::ASSET);
    key.asset().code = assetCode;
    if (cachedEntryExists(key))
    {
        auto entry = getCachedEntry(key);
        return entry ? std::make_shared<AssetFrame>(*entry) : nullptr;
    }

    Database& db = getDatabase();

    string sql = mAssetColumnSelector;
    sql += " WHERE code = :code";
    auto prep = db.getPreparedStatement(sql);
    auto& st = prep.statement();
    st.exchange(use(assetCode));

    auto timer = db.getSelectTimer("load-asset");
    AssetFrame::pointer retAsset;
    loadAssets(prep, [&retAsset](LedgerEntry const& entry)
    {
        retAsset = make_shared<AssetFrame>(entry);
    });

    if (retAsset == nullptr)
    {
        putCachedEntry(key, nullptr);
        return nullptr;
    }

    mStorageHelper.getLedgerDelta().recordEntry(*retAsset);
    putCachedEntry(key, make_shared<LedgerEntry>(retAsset->mEntry));

    return retAsset;
}

AssetFrame::pointer
AssetHelperImpl::loadAsset(AssetCode assetCode, AccountID owner)
{
    auto assetFrame = loadAsset(assetCode);
    if (assetFrame == nullptr)
    {
        return nullptr;
    }

    if (assetFrame->getOwner() == owner)
    {
        return assetFrame;
    }

    return nullptr;
}

void
AssetHelperImpl::loadAssets(StatementContext& prep,
                            function<void(LedgerEntry const&)> assetProcessor)
{
    LedgerEntry le;
    le.data.type(LedgerEntryType::ASSET);
    AssetEntry& oe = le.data.asset();

    int32_t version = 0;

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
    st.exchange(into(version));
    st.define_and_bind();
    st.execute(true);

    while (st.got_data())
    {
        oe.ext.v(static_cast<LedgerVersion>(version));

        AssetFrame::ensureValid(oe);

        assetProcessor(le);
        st.fetch();
    }
}

Database&
AssetHelperImpl::getDatabase()
{
    return mStorageHelper.getDatabase();
}
} // namespace stellar

