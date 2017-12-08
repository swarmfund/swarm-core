//
// Created by kirill on 05.12.17.
//

#include "StatisticsHelper.h"
#include "LedgerDelta.h"
#include <lib/xdrpp/xdrpp/printer.h>

using namespace std;
using namespace soci;

namespace stellar {
    static const char *statisticsColumnSelector =
            "SELECT account_id, daily_out, weekly_out, monthly_out, annual_out, updated_at, lastmodified, version "
                    "FROM   statistics";

    void StatisticsHelper::dropAll(Database &db) {
        db.getSession() << "DROP TABLE IF EXISTS statistics;";
        db.getSession() << "CREATE TABLE statistics"
                "("
                "account_id       VARCHAR(56) NOT NULL,"
                "daily_out        BIGINT 	  NOT NULL,"
                "weekly_out  	  BIGINT 	  NOT NULL,"
                "monthly_out      BIGINT 	  NOT NULL,"
                "annual_out	      BIGINT 	  NOT NULL,"
                "updated_at       BIGINT 	  NOT NULL,"
                "lastmodified     INT 		  NOT NULL,"
                "version		  INT 		  NOT NULL	DEFAULT 0,"
                "PRIMARY KEY  (account_id)"
                ");";
    }

    void StatisticsHelper::storeAdd(LedgerDelta &delta, Database &db, LedgerEntry const &entry) {
        storeUpdateHelper(delta, db, true, entry);
    }

    void StatisticsHelper::storeChange(LedgerDelta &delta, Database &db, LedgerEntry const &entry) {
        storeUpdateHelper(delta, db, false, entry);
    }

    void StatisticsHelper::storeDelete(LedgerDelta &delta, Database &db, LedgerKey const &key) {
        return;
    }

    bool StatisticsHelper::exists(Database &db, LedgerKey const &key) {
        std::string strAccountID = PubKeyUtils::toStrKey(key.stats().accountID);
        int exists = 0;
        auto timer = db.getSelectTimer("statistics-exists");
        auto prep =
                db.getPreparedStatement("SELECT EXISTS (SELECT NULL FROM statistics WHERE account_id=:id)");
        auto &st = prep.statement();
        st.exchange(use(strAccountID));
        st.exchange(into(exists));
        st.define_and_bind();
        st.execute(true);
        return exists != 0;
    }

    LedgerKey StatisticsHelper::getLedgerKey(LedgerEntry const &from) {
        LedgerKey ledgerKey;
        ledgerKey.type(from.data.type());
        ledgerKey.stats().accountID = from.data.stats().accountID;
        return ledgerKey;
    }

    EntryFrame::pointer StatisticsHelper::storeLoad(LedgerKey const &key, Database &db) {
        return loadStatistics(key.stats().accountID, db);
    }

    EntryFrame::pointer StatisticsHelper::fromXDR(LedgerEntry const &from) {
        return std::make_shared<StatisticsFrame>(from);
    }

    uint64_t StatisticsHelper::countObjects(soci::session &sess) {
        uint64_t count = 0;
        sess << "SELECT COUNT(*) FROM statistics;", into(count);
        return count;
    }

    void StatisticsHelper::storeUpdateHelper(LedgerDelta &delta, Database &db, bool insert, const LedgerEntry &entry) 
	{
        auto statisticsFrame = make_shared<StatisticsFrame>(entry);
		auto statisticsEntry = statisticsFrame->getStatistics();

        statisticsFrame->touch(delta);

        bool isValid = statisticsFrame->isValid();

        if (!isValid) {
            CLOG(ERROR, Logging::ENTRY_LOGGER) << "Unexpected state - statistics is invalid: "
                                               << xdr::xdr_to_string(statisticsEntry);
            throw std::runtime_error("Unexpected state - asset is invalid");
        }

        std::string strAccountID = PubKeyUtils::toStrKey(statisticsFrame->getAccountID());
        int32_t statisticsVersion = static_cast<int32_t >(statisticsFrame->getVersion());

        string sql;

        if (insert) {
            sql = "INSERT INTO statistics (account_id, daily_out, "
                    "weekly_out, monthly_out, annual_out, updated_at, lastmodified, version) "
                    "VALUES "
                    "(:aid, :d_out, :w_out, :m_out, :a_out, :up, :lm, :v)";
        } else {
            sql = "UPDATE statistics "
                    "SET 	  daily_out=:d_out, weekly_out=:w_out, monthly_out=:m_out, annual_out=:a_out, "
                    "updated_at=:up, lastmodified=:lm, version=:v "
                    "WHERE  account_id=:aid";
        }

        auto prep = db.getPreparedStatement(sql);
        auto &st = prep.statement();

        st.exchange(use(strAccountID, "aid"));
        st.exchange(use(statisticsEntry.dailyOutcome, "d_out"));
        st.exchange(use(statisticsEntry.weeklyOutcome, "w_out"));
        st.exchange(use(statisticsEntry.monthlyOutcome, "m_out"));
        st.exchange(use(statisticsEntry.annualOutcome, "a_out"));
        st.exchange(use(statisticsEntry.updatedAt, "up"));
        st.exchange(use(statisticsFrame->mEntry.lastModifiedLedgerSeq, "lm"));
        st.exchange(use(statisticsVersion, "v"));
        st.define_and_bind();

        auto timer =
                insert ? db.getInsertTimer("statistics") : db.getUpdateTimer("statistics");
        st.execute(true);

        if (st.get_affected_rows() != 1) {
            throw std::runtime_error("could not update SQL");
        }

        if (insert) {
            delta.addEntry(*statisticsFrame);
        } else {
            delta.modEntry(*statisticsFrame);
        }
    }

    void StatisticsHelper::loadStatistics(StatementContext &prep,
                                          std::function<void(LedgerEntry const &)> statisticsProcessor) {
        std::string accountID;
        LedgerEntry le;
        le.data.type(LedgerEntryType::STATISTICS);
        StatisticsEntry &se = le.data.stats();
        int32_t statisticsVersion = 0;
        statement &st = prep.statement();
        st.exchange(into(accountID));
        st.exchange(into(se.dailyOutcome));
        st.exchange(into(se.weeklyOutcome));
        st.exchange(into(se.monthlyOutcome));
        st.exchange(into(se.annualOutcome));
        st.exchange(into(se.updatedAt));
        st.exchange(into(le.lastModifiedLedgerSeq));
        st.exchange(into(statisticsVersion));
        st.define_and_bind();

        st.execute(true);
        while (st.got_data()) {
            se.accountID = PubKeyUtils::fromStrKey(accountID);
            se.ext.v((LedgerVersion)statisticsVersion);
            bool isValid = StatisticsFrame::isValid(se);
            if (!isValid) {
                CLOG(ERROR, Logging::ENTRY_LOGGER)
                        << "Unexpected state - statistics is invalid: "
                        << xdr::xdr_to_string(se);
                throw std::runtime_error("Unexpected state - statistics is invalid");

            }
            statisticsProcessor(le);
            st.fetch();
        }
    }

    StatisticsFrame::pointer StatisticsHelper::loadStatistics(AccountID const &accountID, Database &db, LedgerDelta *delta) {
        std::string strAccountID = PubKeyUtils::toStrKey(accountID);

        std::string sql = statisticsColumnSelector;
        sql += " WHERE account_id = :id";
        auto prep = db.getPreparedStatement(sql);
        auto& st = prep.statement();
        st.exchange(use(strAccountID));

        auto timer = db.getSelectTimer("statistics");
        StatisticsFrame::pointer retStatistics;
        loadStatistics(prep, [&retStatistics](LedgerEntry const& statistics)
        {
            retStatistics = std::make_shared<StatisticsFrame>(statistics);
        });

        if (delta && retStatistics)
        {
            delta->recordEntry(*retStatistics);
        }

        return retStatistics;
    }

    StatisticsFrame::pointer
    StatisticsHelper::mustLoadStatistics(AccountID const &accountID, Database &db, LedgerDelta *delta) {
        auto result = loadStatistics(accountID, db, delta);
        if (!result)
        {
            CLOG(ERROR, Logging::ENTRY_LOGGER)
                    << "Unexpected db state. Expected statistics to exists. AccountID "
                    << PubKeyUtils::toStrKey(accountID);
            throw std::runtime_error("Unexpected db state. Expected statistics to exist");
        }
        return result;
    }

}