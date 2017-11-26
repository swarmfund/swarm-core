// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ledger/OfferFrame.h"
#include "LedgerDelta.h"
#include "crypto/SHA.h"
#include "crypto/SecretKey.h"
#include "database/Database.h"
#include "util/types.h"
#include "AssetFrame.h"

using namespace std;
using namespace soci;

namespace stellar
{
	const char* OfferFrame::kSQLCreateStatement1 =
		"CREATE TABLE offer"
		"("
		"owner_id          VARCHAR(56)      NOT NULL,"
		"offer_id           BIGINT           NOT NULL CHECK (offer_id >= 0),"
		"base_asset_code    VARCHAR(16)      NOT NULL,"
		"quote_asset_code   VARCHAR(16)      NOT NULL,"
		"is_buy             BOOLEAN          NOT NULL,"
		"base_amount        BIGINT           NOT NULL CHECK (base_amount > 0),"
		"quote_amount       BIGINT           NOT NULL CHECK (quote_amount > 0),"
		"price              BIGINT           NOT NULL CHECK (price > 0),"
		"fee                BIGINT           NOT NULL CHECK (fee >= 0),"
        "percent_fee                BIGINT           NOT NULL CHECK (percent_fee >= 0),"
		"base_balance_id    VARCHAR(56)      NOT NULL,"
		"quote_balance_id   VARCHAR(56)      NOT NULL,"
		"created_at         BIGINT           NOT NULL,"
		"lastmodified       INT              NOT NULL,"
		"PRIMARY KEY      (offer_id)"
		");";

	const char* OfferFrame::kSQLCreateStatement2 =
		"CREATE INDEX base_quote_price ON offer (base_asset_code, quote_asset_code, is_buy, price);";

	static const char* offerColumnSelector =
		"SELECT owner_id, offer_id, base_asset_code, quote_asset_code, base_amount, quote_amount,"
		"price, fee, percent_fee, is_buy, base_balance_id, quote_balance_id, created_at, lastmodified "
		"FROM offer";

	OfferFrame::OfferFrame() : EntryFrame(OFFER_ENTRY), mOffer(mEntry.data.offer())
	{
	}

	OfferFrame::OfferFrame(LedgerEntry const& from)
		: EntryFrame(from), mOffer(mEntry.data.offer())
	{
	}

	OfferFrame::OfferFrame(OfferFrame const& from) : OfferFrame(from.mEntry)
	{
	}

	OfferFrame&
		OfferFrame::operator=(OfferFrame const& other)
	{
		if (&other != this)
		{
			mOffer = other.mOffer;
			mKey = other.mKey;
			mKeyCalculated = other.mKeyCalculated;
		}
		return *this;
	}

	int64_t OfferFrame::getPrice() const
	{
		return mOffer.price;
	}

	uint64
		OfferFrame::getOfferID() const
	{
		return mOffer.offerID;
	}

	bool
		OfferFrame::isValid(OfferEntry const& oe)
	{
		return AssetFrame::isAssetCodeValid(oe.base) && AssetFrame::isAssetCodeValid(oe.quote) && oe.baseAmount > 0 && oe.quoteAmount > 0 && oe.price > 0 && oe.fee >= 0;
	}

	bool
		OfferFrame::isValid() const
	{
		return isValid(mOffer);
	}

	OfferFrame::pointer
		OfferFrame::loadOffer(AccountID const& ownerID, uint64_t offerID, Database& db,
			LedgerDelta* delta)
	{
		OfferFrame::pointer retOffer;

		std::string actIDStrKey = PubKeyUtils::toStrKey(ownerID);

		std::string sql = offerColumnSelector;
		sql += " WHERE owner_id = :id AND offer_id = :offer_id";
		auto prep = db.getPreparedStatement(sql);
		auto& st = prep.statement();
		st.exchange(use(actIDStrKey));
		st.exchange(use(offerID));

		auto timer = db.getSelectTimer("offer");
		loadOffers(prep, [&retOffer](LedgerEntry const& offer) {
			retOffer = make_shared<OfferFrame>(offer);
		});

		if (delta && retOffer)
		{
			delta->recordEntry(*retOffer);
		}

		return retOffer;
	}

	void
		OfferFrame::loadOffers(StatementContext& prep,
			std::function<void(LedgerEntry const&)> offerProcessor)
	{
		std::string actIDStrKey, baseAssetCode, quoteAssetCode, baseBalanceID, quoteBalanceID;
		int isBuy;

		LedgerEntry le;
		le.data.type(OFFER_ENTRY);
		OfferEntry& oe = le.data.offer();

		statement& st = prep.statement();
		st.exchange(into(actIDStrKey));
		st.exchange(into(oe.offerID));
		st.exchange(into(baseAssetCode));
		st.exchange(into(quoteAssetCode));
		st.exchange(into(oe.baseAmount));
		st.exchange(into(oe.quoteAmount));
		st.exchange(into(oe.price));
		st.exchange(into(oe.fee));
		st.exchange(into(oe.percentFee));
		st.exchange(into(isBuy));
		st.exchange(into(baseBalanceID));
		st.exchange(into(quoteBalanceID));
		st.exchange(into(oe.createdAt));
		st.exchange(into(le.lastModifiedLedgerSeq));
		st.define_and_bind();
		st.execute(true);
		while (st.got_data())
		{
			oe.ownerID = PubKeyUtils::fromStrKey(actIDStrKey);
			oe.base = baseAssetCode;
			oe.quote = quoteAssetCode;
			oe.isBuy = isBuy > 0;
			oe.baseBalance = BalanceKeyUtils::fromStrKey(baseBalanceID);
			oe.quoteBalance = BalanceKeyUtils::fromStrKey(quoteBalanceID);
			if (!isValid(oe))
			{
				throw std::runtime_error("Invalid offer");
			}

			offerProcessor(le);
			st.fetch();
		}
	}

	void OfferFrame::loadOffersWithPriceLower(AssetCode const& base, AssetCode const& quote,
		int64_t price, std::vector<OfferFrame::pointer>& retOffers, Database& db)
	{
		std::string sql = offerColumnSelector;
		sql += " WHERE base_asset_code=:s AND quote_asset_code = :b AND price < :p";

		auto prep = db.getPreparedStatement(sql);
		auto& st = prep.statement();

		std::string baseAssetCode = base;
		std::string quoteAssetCode = quote;

		st.exchange(use(baseAssetCode));
		st.exchange(use(quoteAssetCode));
		st.exchange(use(price));

		auto timer = db.getSelectTimer("offer");
		loadOffers(prep, [&retOffers](LedgerEntry const& of) {
			retOffers.emplace_back(make_shared<OfferFrame>(of));
		});
	}

void OfferFrame::loadBestOffers(size_t numOffers, size_t offset, AssetCode const& base, AssetCode const& quote,
	bool isBuy, vector<OfferFrame::pointer>& retOffers, Database& db)
	{
		std::string sql = offerColumnSelector;
		sql += " WHERE base_asset_code=:s AND quote_asset_code = :b AND is_buy=:ib";

		sql += " ORDER BY price ";
		sql += isBuy ? "DESC" : "ASC";

		sql += ", offer_id ASC LIMIT :n OFFSET :o";

		auto prep = db.getPreparedStatement(sql);
		auto& st = prep.statement();

		std::string baseAssetCode = base;
		std::string quoteAssetCode = quote;

		int isBuyRaw = isBuy ? 1 : 0;

		st.exchange(use(baseAssetCode));
		st.exchange(use(quoteAssetCode));
		st.exchange(use(isBuyRaw));
		st.exchange(use(numOffers));
		st.exchange(use(offset));

		auto timer = db.getSelectTimer("offer");
		loadOffers(prep, [&retOffers](LedgerEntry const& of) {
			retOffers.emplace_back(make_shared<OfferFrame>(of));
		});
	}

	std::unordered_map<AccountID, std::vector<OfferFrame::pointer>>
		OfferFrame::loadAllOffers(Database& db)
	{
		std::unordered_map<AccountID, std::vector<OfferFrame::pointer>> retOffers;
		std::string sql = offerColumnSelector;
		sql += " ORDER BY owner_id";
		auto prep = db.getPreparedStatement(sql);

		auto timer = db.getSelectTimer("offer");
		loadOffers(prep, [&retOffers](LedgerEntry const& of) {
			auto& thisUserOffers = retOffers[of.data.offer().ownerID];
			thisUserOffers.emplace_back(make_shared<OfferFrame>(of));
		});
		return retOffers;
	}

	bool
		OfferFrame::exists(Database& db, LedgerKey const& key)
	{
		std::string actIDStrKey = PubKeyUtils::toStrKey(key.offer().ownerID);
		int exists = 0;
		auto timer = db.getSelectTimer("offer-exists");
		auto prep =
			db.getPreparedStatement("SELECT EXISTS (SELECT NULL FROM offer WHERE owner_id=:id AND offer_id=:s)");
		auto& st = prep.statement();
		st.exchange(use(actIDStrKey));
		st.exchange(use(key.offer().offerID));
		st.exchange(into(exists));
		st.define_and_bind();
		st.execute(true);
		return exists != 0;
	}

	uint64_t
		OfferFrame::countObjects(soci::session& sess)
	{
		uint64_t count = 0;
		sess << "SELECT COUNT(*) FROM offer;", into(count);
		return count;
	}

	void
		OfferFrame::storeDelete(LedgerDelta& delta, Database& db) const
	{
		storeDelete(delta, db, getKey());
	}

	void
		OfferFrame::storeDelete(LedgerDelta& delta, Database& db, LedgerKey const& key)
	{
		auto timer = db.getDeleteTimer("offer");
		auto prep = db.getPreparedStatement("DELETE FROM offer WHERE offer_id=:s");
		auto& st = prep.statement();
		st.exchange(use(key.offer().offerID));
		st.define_and_bind();
		st.execute(true);
		delta.deleteEntry(key);
	}

	void
		OfferFrame::storeChange(LedgerDelta& delta, Database& db)
	{
		storeUpdateHelper(delta, db, false);
	}

	void
		OfferFrame::storeAdd(LedgerDelta& delta, Database& db)
	{
		storeUpdateHelper(delta, db, true);
	}

	void
		OfferFrame::storeUpdateHelper(LedgerDelta& delta, Database& db, bool insert)
	{
		touch(delta);

		if (!isValid())
		{
			throw std::runtime_error("Invalid offer");
		}

		string sql;

		if (insert)
		{
			sql = "INSERT INTO offer (owner_id, offer_id,"
				"base_asset_code, quote_asset_code, base_amount, quote_amount,"
				"price, fee, percent_fee, is_buy, "
				"base_balance_id, quote_balance_id, created_at, lastmodified) VALUES "
				"(:sid, :oid, :sac, :bac, :ba, :qa, :p, :f, :pf, :ib, :sbi, :bbi, :ca, :l)";
		}
		else
		{
			sql = "UPDATE offer SET base_asset_code=:sac "
				", quote_asset_code=:bac, base_amount=:ba, quote_amount=:qa,"
				"price=:p, fee=:f, percent_fee=:pf, is_buy=:ib,"
				"base_balance_id=:sbi, quote_balance_id=:bbi, created_at=:ca,"
				"lastmodified=:l WHERE offer_id=:oid";
		}

		auto prep = db.getPreparedStatement(sql);
		auto& st = prep.statement();

		std::string actIDStrKey = PubKeyUtils::toStrKey(mOffer.ownerID);
		std::string baseAssetCode = mOffer.base;
		std::string quoteAssetCode = mOffer.quote;
		std::string quoteBalanceID = BalanceKeyUtils::toStrKey(mOffer.quoteBalance);
		std::string baseBalanceID = BalanceKeyUtils::toStrKey(mOffer.baseBalance);

		if (insert)
		{
			st.exchange(use(actIDStrKey, "sid"));
		}
		st.exchange(use(mOffer.offerID, "oid"));
		st.exchange(use(baseAssetCode, "sac"));
		st.exchange(use(quoteAssetCode, "bac"));
		st.exchange(use(mOffer.baseAmount, "ba"));
		st.exchange(use(mOffer.quoteAmount, "qa"));
		st.exchange(use(mOffer.price, "p"));
		st.exchange(use(mOffer.fee, "f"));
		st.exchange(use(mOffer.percentFee, "pf"));
		int isBuy = mOffer.isBuy ? 1 : 0;
		st.exchange(use(isBuy, "ib"));
		st.exchange(use(baseBalanceID, "sbi"));
		st.exchange(use(quoteBalanceID, "bbi"));
		st.exchange(use(mOffer.createdAt, "ca"));
		st.exchange(use(getLastModified(), "l"));
		st.define_and_bind();

		auto timer =
			insert ? db.getInsertTimer("offer") : db.getUpdateTimer("offer");
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

	void
		OfferFrame::dropAll(Database& db)
	{
		db.getSession() << "DROP TABLE IF EXISTS offer;";
		db.getSession() << kSQLCreateStatement1;
		db.getSession() << kSQLCreateStatement2;
	}
}
