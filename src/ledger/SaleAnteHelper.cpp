#include "SaleAnteHelper.h"
#include "LedgerDelta.h"
#include "xdrpp/printer.h"

using namespace soci;
using namespace std;

namespace stellar {
    const char *saleAnteSelector = "SELECT sale_id, participant_balance_id, amount, lastmodified, version "
                                   "FROM sale_ante";

    void SaleAnteHelper::dropAll(Database &db) {
        db.getSession() << "DROP TABLE IF EXISTS sale_ante";
        db.getSession() << "CREATE TABLE sale_ante"
                           "("
                           "sale_id                BIGINT      NOT NULL CHECK (sale_id >= 0),"
                           "participant_balance_id VARCHAR(56) NOT NULL,"
                           "amount                 BIGINT      NOT NULL CHECK (amount >= 0),"
                           "lastmodified           INT         NOT NULL,"
                           "version                INT         NOT NULL,"
                           "PRIMARY KEY (sale_id, participant_balance_id)"
                           ");";
    }

    void SaleAnteHelper::storeUpdateHelper(LedgerDelta &delta, Database &db, bool insert, LedgerEntry const &entry) {
        auto saleAnteFrame = make_shared<SaleAnteFrame>(entry);
        saleAnteFrame->touch(delta);
        saleAnteFrame->ensureValid();

        auto const key = saleAnteFrame->getKey();
        flushCachedEntry(key, db);

        auto const saleAnteEntry = saleAnteFrame->getSaleAnteEntry();
        auto version = static_cast<int32_t>(saleAnteEntry.ext.v());

        string sql = insert
                     ? "INSERT INTO sale_ante (sale_id, participant_balance_id, amount, lastmodified, version) "
                       "VALUES (:sale_id, :pbid, :amount, :lm, :v)"
                     : "UPDATE sale_ante SET amount = :amount, lastmodified = :lm, version = :v "
                       "WHERE sale_id = :sale_id AND participant_balance_id = :pbid";

        auto prep = db.getPreparedStatement(sql);
        auto &st = prep.statement();

        st.exchange(use(saleAnteEntry.saleID, "sale_id"));
        st.exchange(use(saleAnteEntry.participantBalanceID, "pbid"));
        st.exchange(use(saleAnteEntry.amount, "amount"));
        st.exchange(use(saleAnteFrame->mEntry.lastModifiedLedgerSeq, "lm"));
        st.exchange(use(version, "v"));

        st.define_and_bind();

        auto timer = insert ? db.getInsertTimer("sale_ante") : db.getUpdateTimer("sale-ante");

        st.execute(true);

        if (st.get_affected_rows() != 1) {
            CLOG(ERROR, Logging::ENTRY_LOGGER) << "Failed to update sale ante with sale id: " << saleAnteEntry.saleID
                                               << " and participant balance id: "
                                               << xdr::xdr_to_string(saleAnteEntry.participantBalanceID);
            throw runtime_error("Failed to update sale ante");
        }

        if (insert) {
            delta.addEntry(*saleAnteFrame);
        } else {
            delta.modEntry(*saleAnteFrame);
        }
    }

    void SaleAnteHelper::storeAdd(LedgerDelta &delta, Database &db, LedgerEntry const &entry) {
        storeUpdateHelper(delta, db, true, entry);
    }

    void SaleAnteHelper::storeChange(LedgerDelta &delta, Database &db, LedgerEntry const &entry) {
        storeUpdateHelper(delta, db, false, entry);
    }

    void SaleAnteHelper::storeDelete(LedgerDelta &delta, Database &db, LedgerKey const &key) {
        flushCachedEntry(key, db);
        auto timer = db.getDeleteTimer("sale_ante");
        auto prep = db.getPreparedStatement("DELETE FROM sale_ante WHERE sale_id = :sale_id AND "
                                            "participant_balance_id = :pbid");
        auto &st = prep.statement();
        st.exchange(use(key.saleAnte().saleID, "sale_id"));
        st.exchange(use(key.saleAnte().participantBalanceID, "pbid"));
        st.execute(true);
        delta.deleteEntry(key);
    }

    bool SaleAnteHelper::exists(Database &db, LedgerKey const &key) {
        if (cachedEntryExists(key, db)) {
            return true;
        }

        auto timer = db.getSelectTimer("sale_ante_exists");
        auto prep = db.getPreparedStatement("SELECT EXISTS (SELECT NULL FROM sale_ante WHERE sale_id = :sale_id AND "
                                            "participant_balance_id = :pbid)");
        auto &st = prep.statement();
        st.exchange(use(key.saleAnte().saleID, "sale_id"));
        st.exchange(use(key.saleAnte().participantBalanceID, "pbid"));
        auto exists = 0;
        st.exchange(into(exists));
        st.define_and_bind();
        st.execute(true);

        return exists != 0;
    }

    LedgerKey SaleAnteHelper::getLedgerKey(LedgerEntry const &from) {
        LedgerKey ledgerKey;
        ledgerKey.type(from.data.type());
        ledgerKey.saleAnte().saleID = from.data.saleAnte().saleID;
        ledgerKey.saleAnte().participantBalanceID = from.data.saleAnte().participantBalanceID;
        return ledgerKey;
    }

    void SaleAnteHelper::loadSaleAntes(Database &db, StatementContext &prep,
                                       std::function<void(LedgerEntry const &)> saleAnteProcessor) const {
        try {
            LedgerEntry le;
            le.data.type(LedgerEntryType::SALE_ANTE);
            auto &oe = le.data.saleAnte();
            int version;

            auto &st = prep.statement();
            st.exchange(into(oe.saleID));
            st.exchange(into(oe.participantBalanceID));
            st.exchange(into(oe.amount));
            st.exchange(into(le.lastModifiedLedgerSeq));
            st.exchange(into(version));
            st.define_and_bind();
            st.execute(true);

            while (st.got_data()) {
                oe.ext.v(static_cast<LedgerVersion>(version));
                SaleAnteFrame::ensureValid(oe);

                saleAnteProcessor(le);
                st.fetch();
            }
        } catch (...) {
            throw_with_nested(runtime_error("Failed to load sale ante"));
        }
    }

    SaleAnteFrame::pointer
    SaleAnteHelper::loadSaleAnte(uint64 saleID, BalanceID const &participantBalanceID,
                                 Database &db, LedgerDelta *delta) {
        LedgerKey key;
        key.type(LedgerEntryType::SALE_ANTE);
        key.saleAnte().saleID = saleID;
        key.saleAnte().participantBalanceID = participantBalanceID;

        if (cachedEntryExists(key, db)) {
            const auto p = getCachedEntry(key, db);
            auto result = p ? make_shared<SaleAnteFrame>(*p) : nullptr;
            if (!!delta && !!result) {
                delta->recordEntry(*result);
            }
            return result;
        }

        string sql = saleAnteSelector;
        sql += " WHERE sale_id = :sale_id AND participant_balance_id = :pbid";
        auto prep = db.getPreparedStatement(sql);
        auto &st = prep.statement();
        st.exchange(use(saleID, "sale_id"));
        st.exchange(use(participantBalanceID, "pbid"));

        SaleAnteFrame::pointer retSaleAnte;
        auto timer = db.getSelectTimer("sale_ante");
        loadSaleAntes(db, prep, [&retSaleAnte](LedgerEntry const &entry) {
            retSaleAnte = make_shared<SaleAnteFrame>(entry);
        });

        if (!retSaleAnte) {
            putCachedEntry(key, nullptr, db);
            return nullptr;
        }

        if (!!delta) {
            delta->recordEntry(*retSaleAnte);
        }

        const auto pEntry = make_shared<LedgerEntry>(retSaleAnte->mEntry);
        putCachedEntry(key, pEntry, db);
        return retSaleAnte;
    }

    EntryFrame::pointer
    SaleAnteHelper::storeLoad(LedgerKey const &key, Database &db) {
        return loadSaleAnte(key.saleAnte().saleID, key.saleAnte().participantBalanceID, db);
    }

    EntryFrame::pointer SaleAnteHelper::fromXDR(LedgerEntry const &from) {
        return make_shared<SaleAnteFrame>(from);
    }

    uint64_t SaleAnteHelper::countObjects(soci::session &sess) {
        uint64_t count = 0;
        sess << "SELECT COUNT(*) FROM sale_ante;", into(count);
        return count;
    }

    std::vector<SaleAnteFrame::pointer> SaleAnteHelper::loadSaleAntesForSale(uint64_t saleID, Database &db) {
        string sql = saleAnteSelector;
        sql += " WHERE sale_id = :sale_id";
        auto prep = db.getPreparedStatement(sql);
        auto &st = prep.statement();
        st.exchange(use(saleID, "sale_id"));

        vector<SaleAnteFrame::pointer> result;
        auto timer = db.getSelectTimer("sale_ante");
        loadSaleAntes(db, prep, [&result](LedgerEntry const &entry) {
            SaleAnteFrame::pointer retSaleAnte;
            retSaleAnte = make_shared<SaleAnteFrame>(entry);
            result.push_back(retSaleAnte);
        });

        return result;
    }
}