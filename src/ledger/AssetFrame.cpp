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
#include "xdrpp/printer.h"
#include "crypto/Hex.h"
#include <locale>

using namespace soci;
using namespace std;

namespace stellar
{
using xdr::operator<;

static const char* assetColumnSelector =
"SELECT code, owner, name, preissued_asset_signer, description, external_resource_link, max_issuance_amount, available_for_issueance,"
" issued, policies, lastmodified, version FROM asset";

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

AssetFrame::pointer AssetFrame::create(AssetCreationRequest const & request, AccountID const& owner)
{
	LedgerEntry le;
	le.data.type(LedgerEntryType::ASSET);
	AssetEntry& asset = le.data.asset();
	asset.availableForIssueance = 0;
	asset.code = request.code;
	asset.description = request.description;
	asset.externalResourceLink = request.externalResourceLink;
	asset.issued = 0;
	asset.maxIssuanceAmount = request.maxIssuanceAmount;
	asset.name = request.name;
	asset.owner = owner;
	asset.policies = request.policies;
	asset.preissuedAssetSigner = request.preissuedAssetSigner;
	return std::make_shared<AssetFrame>(le);
}

bool AssetFrame::willExceedMaxIssuanceAmount(uint64_t amount) {
	uint64_t issued;
	if (!safeSum(mAsset.issued, amount, issued)) {
		return true;
	}

	return issued > mAsset.maxIssuanceAmount;
}

bool AssetFrame::tryIssue(uint64_t amount) {
	if (willExceedMaxIssuanceAmount(amount)) {
		return false;
	}

	if (!isAvailableForIssuanceAmountSufficient(amount)) {
		return false;
	}

	mAsset.availableForIssueance -= amount;
	mAsset.issued += amount;
	return true;
}

bool AssetFrame::canAddAvailableForIssuance(uint64_t amount) {
	uint64_t availableForIssueance;
	if (!safeSum(mAsset.availableForIssueance, amount, availableForIssueance))
		return false;

	uint64_t maxAmountCanBeIssuedAfterUpdate;
	if (!safeSum(mAsset.issued, availableForIssueance, maxAmountCanBeIssuedAfterUpdate))
		return false;

	if (maxAmountCanBeIssuedAfterUpdate > mAsset.maxIssuanceAmount)
		return false;

	return true;
}

bool AssetFrame::tryAddAvailableForIssuance(uint64_t amount) {
	if (!canAddAvailableForIssuance(amount))
		return false;

	mAsset.availableForIssueance += amount;
	return true;
}

bool AssetFrame::isAssetCodeValid(AssetCode const & code)
{
	bool zeros = false;
	bool onechar = false; // at least one non zero character
	for (uint8_t b : code)
	{
		if (b == 0)
		{
			zeros = true;
		}
		else if (zeros)
		{
			// zeros can only be trailing
			return false;
		}
		else
		{
			if (b > 0x7F || !std::isalnum((char)b, cLocale))
			{
				return false;
			}
			onechar = true;
		}
	}
	return onechar;
}

bool
AssetFrame::isValid(AssetEntry const& oe)
{
	uint64_t canBeIssued;
	if (!safeSum(oe.issued, oe.availableForIssueance, canBeIssued)) {
		return false;
	}

	return isAssetCodeValid(oe.code) && oe.maxIssuanceAmount >= canBeIssued;
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

AssetFrame::pointer AssetFrame::loadAsset(AssetCode code, AccountID const & owner, Database & db, LedgerDelta * delta)
{
	auto assetFrame = loadAsset(code, db, delta);
	if (!assetFrame)
		return nullptr;

	if (assetFrame->getAsset().owner == owner) {
		return assetFrame;
	}

	return nullptr;
}

void
AssetFrame::loadAssets(StatementContext& prep,
                       std::function<void(LedgerEntry const&)> assetProcessor)
{
    LedgerEntry le;
    le.data.type(ASSET);
    AssetEntry& oe = le.data.asset();
    string code, owner, name, preissuedAssetSigner, description, externalResourceLink;
    int32_t assetVersion;

    statement& st = prep.statement();
    st.exchange(into(code));
	st.exchange(into(owner));
	st.exchange(into(name));
	st.exchange(into(preissuedAssetSigner));
	st.exchange(into(description));
	st.exchange(into(externalResourceLink));
	st.exchange(into(oe.maxIssuanceAmount));
	st.exchange(into(oe.availableForIssueance));
	st.exchange(into(oe.issued));
    st.exchange(into(oe.policies));
    st.exchange(into(le.lastModifiedLedgerSeq));
	st.exchange(into(assetVersion));
    st.define_and_bind();
    st.execute(true);
    while (st.got_data())
    {
        oe.ext.v((LedgerVersion)assetVersion);
        oe.code = code;
		oe.owner = PubKeyUtils::fromStrKey(owner);
		oe.name = name;
		oe.preissuedAssetSigner = PubKeyUtils::fromStrKey(preissuedAssetSigner);
		oe.description = description;
		oe.externalResourceLink = externalResourceLink;
		oe.ext.v((LedgerVersion)assetVersion);

        if (!isValid(oe))
        {
			CLOG(ERROR, Logging::ENTRY_LOGGER) << "Unexpected state - asset is invalid: " << xdr::xdr_to_string(oe);
			throw std::runtime_error("Unexpected state - asset is invalid");
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
		CLOG(ERROR, Logging::ENTRY_LOGGER) << "Unexpected state - asset is invalid: " << xdr::xdr_to_string(mAsset);
		throw std::runtime_error("Unexpected state - asset is invalid");
    }

	flushCachedEntry(db);

    string assetCode = mAsset.code;
	string owner = PubKeyUtils::toStrKey(mAsset.owner);
	string name = mAsset.name;
	string preissuedAssetSigner = PubKeyUtils::toStrKey(mAsset.preissuedAssetSigner);
	string desc = mAsset.description;
	string externalLink = mAsset.externalResourceLink;
    int32_t assetVersion = mAsset.ext.v();

    string sql;

    if (insert)
    {
		sql = "INSERT INTO asset (code, owner, name, preissued_asset_signer, description, external_resource_link, max_issuance_amount,"
			"available_for_issueance, issued, policies, lastmodified, version) "
			"VALUES (:code, :owner, :name, :preissued_asset_signer, :description, :external_resource_link, :max_issuance_amount, "
			":available_for_issueance, :issued, :policies, :lm, :v)";
    }
    else
    {
        sql = "UPDATE asset SET owner = :owner, name = :name, preissued_asset_signer = :preissued_asset_signer, description = :description,"
			"external_resource_link = :external_resource_link, max_issuance_amount = :max_issuance_amount,"
			"available_for_issueance = :available_for_issueance, issued = :issued, policies = :policies, lastmodified = :lm, version = :v "
            "WHERE code = :code";
    }

    auto prep = db.getPreparedStatement(sql);
    auto& st = prep.statement();

    st.exchange(use(assetCode, "code"));
	st.exchange(use(owner, "owner"));
	st.exchange(use(name, "name"));
	st.exchange(use(preissuedAssetSigner, "preissued_asset_signer"));
	st.exchange(use(desc, "description"));
	st.exchange(use(externalLink, "external_resource_link"));
	st.exchange(use(mAsset.maxIssuanceAmount, "max_issuance_amount"));
	st.exchange(use(mAsset.availableForIssueance, "available_for_issueance"));
	st.exchange(use(mAsset.issued, "issued"));
    st.exchange(use(mAsset.policies, "policies"));
	st.exchange(use(mEntry.lastModifiedLedgerSeq, "lm"));
	st.exchange(use(assetVersion, "v"));
    st.define_and_bind();

    auto timer = insert ? db.getInsertTimer("asset") : db.getUpdateTimer("asset");

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
    db.getSession() << "CREATE TABLE asset"
		"("
		"code                    VARCHAR(16)   NOT NULL,"
		"owner                   VARCHAR(56)   NOT NULL,"
		"name                    VARCHAR(64)   NOT NULL,"
		"preissued_asset_signer  VARCHAR(56)   NOT NULL,"
		"description             TEXT          NOT NULL,"
		"external_resource_link  VARCHAR(256)  NOT NULL,"
		"max_issuance_amount     NUMERIC(20,0) NOT NULL CHECK (max_issuance_amount >= 0),"
		"available_for_issueance NUMERIC(20,0) NOT NULL CHECK (available_for_issueance >= 0),"
		"issued                  NUMERIC(20,0) NOT NULL CHECK (issued >= 0),"
		"policies                INT           NOT NULL, "
		"lastmodified            INT           NOT NULL, "
		"version                 INT           NOT NULL, "
		"PRIMARY KEY (code)"
		");";
}
}

