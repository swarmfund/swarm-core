// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "AssetHelper.h"
#include "LedgerDelta.h"
#include "xdrpp/printer.h"

using namespace soci;
using namespace std;

namespace stellar
{
	using xdr::operator<;

	static const char* assetColumnSelector =
		"SELECT code, owner, name, preissued_asset_signer, description, external_resource_link, max_issuance_amount, available_for_issueance,"
		" issued, policies, lastmodified, version FROM asset";

	void
	AssetHelper::dropAll(Database& db)
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

	void
	AssetHelper::storeUpdateHelper(LedgerDelta& delta, Database& db, bool insert, LedgerEntry const& entry)
	{
		auto assetFrame = make_shared<AssetFrame>(entry);

		assetFrame->touch(delta);

		bool isValid = assetFrame->isValid();
		if (!isValid)
		{
			auto asset = assetFrame->getAsset();
			CLOG(ERROR, Logging::ENTRY_LOGGER) << "Unexpected state - asset is invalid: " << xdr::xdr_to_string(asset);
			throw std::runtime_error("Unexpected state - asset is invalid");
		}
		
		auto key = assetFrame->getKey();
		flushCachedEntry(key, db);

		string assetCode = assetFrame->getCode();
		string owner = PubKeyUtils::toStrKey(assetFrame->getOwner());
		string name = assetFrame->getAsset().name;
		string preissuedAssetSigner = PubKeyUtils::toStrKey(assetFrame->getPreIssuedAssetSigner());
		string desc = assetFrame->getAsset().description;
		string externalLink = assetFrame->getAsset().externalResourceLink;
		auto assetVersion = static_cast<int32_t>(assetFrame->getAsset().ext.v());

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
		st.exchange(use(assetFrame->getMaxIssuanceAmount(), "max_issuance_amount"));
		st.exchange(use(assetFrame->getAvailableForIssuance(), "available_for_issueance"));
		st.exchange(use(assetFrame->getIssued(), "issued"));
		st.exchange(use(assetFrame->getPolicies(), "policies"));
		st.exchange(use(assetFrame->getLastModified(), "lm"));
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
			delta.addEntry(*assetFrame);
		}
		else
		{
			delta.modEntry(*assetFrame);
		}

	}

	void 
	AssetHelper::storeAdd(LedgerDelta& delta, Database& db, LedgerEntry const& entry)
	{
		storeUpdateHelper(delta, db, true, entry);
	}

	void
	AssetHelper::storeChange(LedgerDelta& delta, Database& db, LedgerEntry const& entry)
	{
		storeUpdateHelper(delta, db, false, entry);
	}

	void 
	AssetHelper::storeDelete(LedgerDelta& delta, Database& db, LedgerKey const& key)
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

	bool
	AssetHelper::exists(Database& db, LedgerKey const& key)
	{
		return AssetHelper::exists(db, key.asset().code);
	}

	bool
	AssetHelper::exists(Database& db, AssetCode code)
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

	LedgerKey
	AssetHelper::getLedgerKey(LedgerEntry const& from)
	{
		LedgerKey ledgerKey;
		ledgerKey.type(from.data.type());
		ledgerKey.asset().code = from.data.asset().code;
		return ledgerKey;
	}

	EntryFrame::pointer
	AssetHelper::storeLoad(LedgerKey const& key, Database& db)
	{
		return loadAsset(key.asset().code, db);
	}

	EntryFrame::pointer
	AssetHelper::fromXDR(LedgerEntry const& from)
	{
		return std::make_shared<AssetFrame>(from);
	}

	uint64_t
	AssetHelper::countObjects(soci::session& sess)
	{
		uint64_t count = 0;
		sess << "SELECT COUNT(*) FROM asset;", into(count);
		return count;
	}

	AssetFrame::pointer
	AssetHelper::loadAsset(AssetCode code, Database& db,
			LedgerDelta* delta)
	{
		LedgerKey key;
		key.type(LedgerEntryType::ASSET);
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

		auto pEntry = std::make_shared<LedgerEntry const>(retAsset->mEntry);
		putCachedEntry(key, pEntry, db);
		return retAsset;
	}

	AssetFrame::pointer 
	AssetHelper::loadAsset(AssetCode code, AccountID const & owner, Database & db, LedgerDelta * delta)
	{
		auto assetFrame = loadAsset(code, db, delta);
		if (!assetFrame)
			return nullptr;

		if (assetFrame->getAsset().owner == owner) {
			return assetFrame;
		}

		return nullptr;
	}

	void AssetHelper::loadAssets(std::vector<AssetFrame::pointer>& retAssets,
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

	void AssetHelper::loadAssets(StatementContext& prep,
			std::function<void(LedgerEntry const&)> assetProcessor)
	{
		LedgerEntry le;
		le.data.type(LedgerEntryType::ASSET);
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

			bool isValid = AssetFrame::isValid(oe);
			if (!isValid)
			{
				CLOG(ERROR, Logging::ENTRY_LOGGER) << "Unexpected state - asset is invalid: " << xdr::xdr_to_string(oe);
				throw std::runtime_error("Unexpected state - asset is invalid");
			}

			assetProcessor(le);
			st.fetch();
		}
	}

}