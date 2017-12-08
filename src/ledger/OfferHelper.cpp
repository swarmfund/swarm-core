// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "OfferHelper.h"
#include "LedgerDelta.h"
#include "xdrpp/printer.h"

using namespace soci;
using namespace std;

namespace stellar {
    using xdr::operator<;

    static const char* offerColumnSelector =
            "SELECT owner_id, offer_id, base_asset_code, quote_asset_code, base_amount, quote_amount,"
                    "price, fee, percent_fee, is_buy, base_balance_id, quote_balance_id, created_at, lastmodified, version "
                    "FROM offer";

    void OfferHelper::dropAll(Database &db) {
        db.getSession() << "DROP TABLE IF EXISTS offer;";
        db.getSession() << "CREATE TABLE offer"
                "("
                "owner_id          	VARCHAR(56)      NOT NULL,"
                "offer_id           BIGINT           NOT NULL CHECK (offer_id >= 0),"
                "base_asset_code    VARCHAR(16)      NOT NULL,"
                "quote_asset_code   VARCHAR(16)      NOT NULL,"
                "is_buy             BOOLEAN          NOT NULL,"
                "base_amount        BIGINT           NOT NULL CHECK (base_amount > 0),"
                "quote_amount       BIGINT           NOT NULL CHECK (quote_amount > 0),"
                "price              BIGINT           NOT NULL CHECK (price > 0),"
                "fee                BIGINT           NOT NULL CHECK (fee >= 0),"
                "percent_fee        BIGINT           NOT NULL CHECK (percent_fee >= 0),"
                "base_balance_id    VARCHAR(56)      NOT NULL,"
                "quote_balance_id   VARCHAR(56)      NOT NULL,"
                "created_at         BIGINT           NOT NULL,"
                "lastmodified       INT              NOT NULL,"
                "version			INT				 NOT NULL DEFAULT 0,"
                "PRIMARY KEY      (offer_id)"
                ");";;
        db.getSession() << "CREATE INDEX base_quote_price ON offer"
                " (base_asset_code, quote_asset_code, is_buy, price);";;
    }

    void OfferHelper::storeAdd(LedgerDelta &delta, Database &db, LedgerEntry const &entry) {
        storeUpdateHelper(delta, db, true, entry);
    }

    void OfferHelper::storeChange(LedgerDelta &delta, Database &db, LedgerEntry const &entry) {
        storeUpdateHelper(delta, db, false, entry);
    }

    void OfferHelper::storeDelete(LedgerDelta &delta, Database &db, LedgerKey const &key) {
        auto timer = db.getDeleteTimer("offer");
        auto prep = db.getPreparedStatement("DELETE FROM offer WHERE offer_id=:s");
        auto& st = prep.statement();
        st.exchange(use(key.offer().offerID));
        st.define_and_bind();
        st.execute(true);
        delta.deleteEntry(key);
    }

    bool OfferHelper::exists(Database &db, LedgerKey const &key) {
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

    LedgerKey OfferHelper::getLedgerKey(LedgerEntry const &from) {
        LedgerKey ledgerKey;
        ledgerKey.type(from.data.type());
        ledgerKey.offer().offerID = from.data.offer().offerID;
        ledgerKey.offer().ownerID = from.data.offer().ownerID;
        return ledgerKey;
    }

    EntryFrame::pointer OfferHelper::storeLoad(LedgerKey const &key, Database &db) {
        return loadOffer(key.offer().ownerID, key.offer().offerID, db);
    }

    EntryFrame::pointer OfferHelper::fromXDR(LedgerEntry const &from) {
        return std::make_shared<OfferFrame>(from);
    }

    uint64_t OfferHelper::countObjects(soci::session &sess) {
        uint64_t count = 0;
        sess << "SELECT COUNT(*) FROM offer;", into(count);
        return count;
    }

    void OfferHelper::storeUpdateHelper(LedgerDelta &delta, Database &db, bool insert, LedgerEntry const &entry) {

        auto offerFrame = make_shared<OfferFrame>(entry);
		auto offerEntry = offerFrame->getOffer();

        offerFrame->touch(delta);

        if (!offerFrame->isValid())
        {
            CLOG(ERROR, Logging::ENTRY_LOGGER)
                    << "Unexpected state - offer is invalid: "
                    << xdr::xdr_to_string(offerFrame->getOffer());
            throw std::runtime_error("Unexpected state - offer is invalid");
        }

        string sql;

        if (insert)
        {
            sql = "INSERT INTO offer (owner_id, offer_id,"
                    "base_asset_code, quote_asset_code, base_amount, quote_amount,"
                    "price, fee, percent_fee, is_buy, "
                    "base_balance_id, quote_balance_id, created_at, lastmodified, version) "
                    "VALUES "
                    "(:sid, :oid, :sac, :bac, :ba, :qa, :p, :f, :pf, :ib, :sbi, :bbi, :ca, :l, :v)";
        }
        else
        {
            sql = "UPDATE offer "
                    "SET 	  base_asset_code=:sac, quote_asset_code=:bac, base_amount=:ba, quote_amount=:qa,"
                    "price=:p, fee=:f, percent_fee=:pf, is_buy=:ib,"
                    "base_balance_id=:sbi, quote_balance_id=:bbi, created_at=:ca,"
                    "lastmodified=:l, version=:v "
                    "WHERE  offer_id=:oid";
        }

        auto prep = db.getPreparedStatement(sql);
        auto& st = prep.statement();

        std::string actIDStrKey = PubKeyUtils::toStrKey(offerEntry.ownerID);
        std::string baseAssetCode = offerEntry.base;
        std::string quoteAssetCode = offerEntry.quote;
        std::string quoteBalanceID = BalanceKeyUtils::toStrKey(offerEntry.quoteBalance);
        std::string baseBalanceID = BalanceKeyUtils::toStrKey(offerEntry.baseBalance);
        auto offerVersion = static_cast<int32_t >(offerEntry.ext.v());

        if (insert)
        {
            st.exchange(use(actIDStrKey, "sid"));
        }
        st.exchange(use(offerEntry.offerID, "oid"));
        st.exchange(use(baseAssetCode, "sac"));
        st.exchange(use(quoteAssetCode, "bac"));
        st.exchange(use(offerEntry.baseAmount, "ba"));
        st.exchange(use(offerEntry.quoteAmount, "qa"));
        st.exchange(use(offerEntry.price, "p"));
        st.exchange(use(offerEntry.fee, "f"));
        st.exchange(use(offerEntry.percentFee, "pf"));
        int isBuy = offerEntry.isBuy ? 1 : 0;
        st.exchange(use(isBuy, "ib"));
        st.exchange(use(baseBalanceID, "sbi"));
        st.exchange(use(quoteBalanceID, "bbi"));
        st.exchange(use(offerEntry.createdAt, "ca"));
        st.exchange(use(offerFrame->mEntry.lastModifiedLedgerSeq, "l"));
        st.exchange(use(offerVersion, "v"));
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
            delta.addEntry(*offerFrame);
        }
        else
        {
            delta.modEntry(*offerFrame);
        }
    }

    void OfferHelper::loadOffers(StatementContext &prep, function<void(const LedgerEntry &)> offerProcessor) {
        std::string actIDStrKey, baseAssetCode, quoteAssetCode, baseBalanceID, quoteBalanceID;
        int isBuy;
        int32_t offerVersion = 0;

        LedgerEntry le;
        le.data.type(LedgerEntryType::OFFER_ENTRY);
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
        st.exchange(into(offerVersion));
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
            oe.ext.v((LedgerVersion)offerVersion);
            if (!OfferFrame::isValid(oe))
            {
                CLOG(ERROR, Logging::ENTRY_LOGGER)
                        << "Unexpected state - offer is invalid: "
                        << xdr::xdr_to_string(oe);
                throw std::runtime_error("Unexpected state - offer is invalid");
            }

            offerProcessor(le);
            st.fetch();
        }
    }

    OfferFrame::pointer
    OfferHelper::loadOffer(AccountID const &accountID, uint64_t offerID, Database &db, LedgerDelta *delta) {
        OfferFrame::pointer retOffer;

        std::string actIDStrKey = PubKeyUtils::toStrKey(accountID);

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

    void OfferHelper::loadOffersWithPriceLower(AssetCode const &base, AssetCode const &quote, int64_t price,
                                               std::vector<OfferFrame::pointer> &retOffers, Database &db) {
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

    std::unordered_map<AccountID, std::vector<OfferFrame::pointer>> OfferHelper::loadAllOffers(Database &db) {
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

    void OfferHelper::loadBestOffers(size_t numOffers, size_t offset, AssetCode const &base, AssetCode const &quote,
                                     bool isBuy, std::vector<OfferFrame::pointer> &retOffers, Database &db) {
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
}