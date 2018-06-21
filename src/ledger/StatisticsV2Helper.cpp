#include "StatisticsV2Helper.h"
#include "LedgerDelta.h"
#include <lib/xdrpp/xdrpp/printer.h>

using namespace std;
using namespace soci;

namespace stellar {
    static const char *statisticsColumnSelector =
            "SELECT id, account_id, stats_op_type, asset_code, is_convert_needed, "
            "daily_out, weekly_out, monthly_out, annual_out, updated_at, lastmodified, version "
            "FROM   statistics_v2";

    void StatisticsV2Helper::dropAll(Database &db) {
        db.getSession() << "DROP TABLE IF EXISTS statistics_v2 CASCADE;";
        db.getSession() << "CREATE TABLE statistics_v2"
                           "("
                           "id                  BIGINT          PRIMARY KEY,"
                           "account_id          VARCHAR(56)     NOT NULL,"
                           "stats_op_type       INT             NOT NULL,"
                           "asset_code          TEXT            NOT NULL,"
                           "is_convert_needed   BOOLEAN         NOT NULL,"
                           "daily_out           NUMERIC(20,0) 	NOT NULL,"
                           "weekly_out          NUMERIC(20,0)  	NOT NULL,"
                           "monthly_out         NUMERIC(20,0)  	NOT NULL,"
                           "annual_out	        NUMERIC(20,0)  	NOT NULL,"
                           "updated_at          BIGINT 	        NOT NULL,"
                           "lastmodified        INT 		    NOT NULL,"
                           "version	            INT 		    NOT NULL DEFAULT 0,"
                           "UNIQUE (account_id, stats_op_type, asset_code, is_convert_needed)"
                           ");";
    }

    void StatisticsV2Helper::storeAdd(LedgerDelta &delta, Database &db, LedgerEntry const &entry) {
        storeUpdateHelper(delta, db, true, entry);
    }

    void StatisticsV2Helper::storeChange(LedgerDelta &delta, Database &db, LedgerEntry const &entry) {
        storeUpdateHelper(delta, db, false, entry);
    }

    void StatisticsV2Helper::storeDelete(LedgerDelta &delta, Database &db, LedgerKey const &key) {}

    bool StatisticsV2Helper::exists(Database &db, LedgerKey const &key) {
        int exists = 0;
        auto timer = db.getSelectTimer("statistics_v2_exists");
        auto prep = db.getPreparedStatement("SELECT EXISTS (SELECT NULL FROM statistics_v2 WHERE account_id=:id)");
        auto &st = prep.statement();
        st.exchange(use(key.statisticsV2().id));
        st.exchange(into(exists));
        st.define_and_bind();
        st.execute(true);
        return exists != 0;
    }

    LedgerKey StatisticsV2Helper::getLedgerKey(LedgerEntry const &from) {
        LedgerKey ledgerKey;
        ledgerKey.type(from.data.type());
        ledgerKey.statisticsV2().id = from.data.statisticsV2().id;
        return ledgerKey;
    }

    EntryFrame::pointer StatisticsV2Helper::storeLoad(LedgerKey const &key, Database &db) {
        return loadStatistics(key.statisticsV2().id, db);
    }

    EntryFrame::pointer StatisticsV2Helper::fromXDR(LedgerEntry const &from) {
        return std::make_shared<StatisticsV2Frame>(from);
    }

    uint64_t StatisticsV2Helper::countObjects(soci::session &sess) {
        uint64_t count = 0;
        sess << "SELECT COUNT(*) FROM statistics_v2;", into(count);
        return count;
    }

    void StatisticsV2Helper::storeUpdateHelper(LedgerDelta &delta, Database &db, bool insert, const LedgerEntry &entry)
    {
        auto statisticsV2Frame = make_shared<StatisticsV2Frame>(entry);
        auto statisticsV2Entry = statisticsV2Frame->getStatistics();

        statisticsV2Frame->touch(delta);

        if (!statisticsV2Frame->isValid()) {
            CLOG(ERROR, Logging::ENTRY_LOGGER) << "Unexpected state - statisticsV2 is invalid: "
                                               << xdr::xdr_to_string(statisticsV2Entry);
            throw std::runtime_error("Unexpected state - statisticsV2 is invalid");
        }

        string strAccountID = PubKeyUtils::toStrKey(statisticsV2Frame->getAccountID());
        auto statisticsVersion = static_cast<int32_t>(statisticsV2Frame->getVersion());
        auto statsOpType = static_cast<int32_t>(statisticsV2Entry.statsOpType);
        int isConvertNeeded = statisticsV2Entry.isConvertNeeded ? 1 : 0;

        string sql;

        if (insert)
        {
            sql = "INSERT INTO statistics_v2 (id, account_id, stats_op_type, asset_code, is_convert_needed, "
                  "daily_out, weekly_out, monthly_out, annual_out, updated_at, lastmodified, version) "
                  "VALUES "
                  "(:id, :aid, :stats_t, :asset_c, :is_c, :d_out, :w_out, :m_out, :a_out, :up, :lm, :v)";
        }
        else
        {
            sql = "UPDATE statistics_v2 "
                  "SET 	  account_id=:aid, stats_op_type=:stats_t, asset_code=:asset_c, is_convert_needed=:is_c, "
                  "daily_out=:d_out, weekly_out=:w_out, monthly_out=:m_out, annual_out=:a_out, "
                  "updated_at=:up, lastmodified=:lm, version=:v "
                  "WHERE  id=:id";
        }

        auto prep = db.getPreparedStatement(sql);
        auto &st = prep.statement();

        st.exchange(use(statisticsV2Entry.id, "id"));
        st.exchange(use(strAccountID, "aid"));
        st.exchange(use(statsOpType, "stats_t"));
        st.exchange(use(statisticsV2Entry.assetCode, "asset_c"));
        st.exchange(use(isConvertNeeded, "is_c"));
        st.exchange(use(statisticsV2Entry.dailyOutcome, "d_out"));
        st.exchange(use(statisticsV2Entry.weeklyOutcome, "w_out"));
        st.exchange(use(statisticsV2Entry.monthlyOutcome, "m_out"));
        st.exchange(use(statisticsV2Entry.annualOutcome, "a_out"));
        st.exchange(use(statisticsV2Entry.updatedAt, "up"));
        st.exchange(use(statisticsV2Frame->mEntry.lastModifiedLedgerSeq, "lm"));
        st.exchange(use(statisticsVersion, "v"));
        st.define_and_bind();

        auto timer = insert ? db.getInsertTimer("statistics-v2")
                            : db.getUpdateTimer("statistics-v2");
        st.execute(true);

        if (st.get_affected_rows() != 1) {
            throw std::runtime_error("could not update SQL");
        }

        if (insert)
            delta.addEntry(*statisticsV2Frame);
        else
            delta.modEntry(*statisticsV2Frame);

    }

    void StatisticsV2Helper::loadStatistics(StatementContext &prep,
                                            function<void(LedgerEntry const &)> processor)
    {
        LedgerEntry le;
        le.data.type(LedgerEntryType::STATISTICS_V2);
        StatisticsV2Entry &se = le.data.statisticsV2();
        string accountID;
        int32_t statsOpType;
        int32_t isConvertNeeded = 0;
        int32_t version = 0;

        statement &st = prep.statement();
        st.exchange(into(se.id));
        st.exchange(into(accountID));
        st.exchange(into(statsOpType));
        st.exchange(into(se.assetCode));
        st.exchange(into(isConvertNeeded));
        st.exchange(into(se.dailyOutcome));
        st.exchange(into(se.weeklyOutcome));
        st.exchange(into(se.monthlyOutcome));
        st.exchange(into(se.annualOutcome));
        st.exchange(into(se.updatedAt));
        st.exchange(into(le.lastModifiedLedgerSeq));
        st.exchange(into(version));

        st.define_and_bind();
        st.execute(true);

        while (st.got_data())
        {
            se.accountID = PubKeyUtils::fromStrKey(accountID);
            se.statsOpType = static_cast<StatsOpType>(statsOpType);
            se.isConvertNeeded = isConvertNeeded > 0;
            se.ext.v((LedgerVersion)version);

            bool isValid = StatisticsV2Frame::isValid(se);
            if (!isValid)
            {
                CLOG(ERROR, Logging::ENTRY_LOGGER) << "Unexpected state - statisticsV2 is invalid: "
                                                   << xdr::xdr_to_string(se);
                throw std::runtime_error("Unexpected state - statisticsV2 is invalid");

            }

            processor(le);
            st.fetch();
        }
    }

    StatisticsV2Frame::pointer
    StatisticsV2Helper::loadStatistics(AccountID& accountID, StatsOpType statsOpType, AssetCode const& assetCode,
                                       bool isConvertNeeded, Database &db, LedgerDelta *delta)
    {
        string strAccountID = PubKeyUtils::toStrKey(accountID);
        auto intStatsOpType = static_cast<int32_t>(statsOpType);
        int intIsConvertNeeded = isConvertNeeded ? 1 : 0;

        string sql = statisticsColumnSelector;
        sql += " WHERE account_id = :acc_id AND stats_op_type = :stats_t AND "
               "       asset_code = :asset_c AND is_convert_needed = :is_c";

        auto prep = db.getPreparedStatement(sql);
        auto& st = prep.statement();
        st.exchange(use(strAccountID, "acc_id"));
        st.exchange(use(intStatsOpType, "stats_t"));
        st.exchange(use(assetCode, "asset_c"));
        st.exchange(use(intIsConvertNeeded, "is_c"));

        auto timer = db.getSelectTimer("statistics_v2");
        StatisticsV2Frame::pointer result;
        loadStatistics(prep, [&result](LedgerEntry const& entry)
        {
            result = std::make_shared<StatisticsV2Frame>(entry);
        });

        if (!result)
            return nullptr;

        if (delta)
            delta->recordEntry(*result);

        return result;
    }

    StatisticsV2Frame::pointer
    StatisticsV2Helper::loadStatistics(uint64_t id, Database &db, LedgerDelta *delta)
    {
        string sql = statisticsColumnSelector;
        sql += " WHERE id = :id";
        auto prep = db.getPreparedStatement(sql);
        auto& st = prep.statement();
        st.exchange(use(id));

        auto timer = db.getSelectTimer("statistics_v2");
        StatisticsV2Frame::pointer result;
        loadStatistics(prep, [&result](LedgerEntry const& entry)
        {
            result = std::make_shared<StatisticsV2Frame>(entry);
        });

        if (!result)
            return nullptr;

        if (delta)
            delta->recordEntry(*result);

        return result;
    }

    StatisticsV2Frame::pointer
    StatisticsV2Helper::mustLoadStatistics(uint64_t id, Database &db, LedgerDelta *delta)
    {
        auto result = loadStatistics(id, db, delta);
        if (!result)
        {
            CLOG(ERROR, Logging::ENTRY_LOGGER)
                    << "Unexpected db state. Expected statisticsV2 to exists. id: "
                    << id;
            throw std::runtime_error("Unexpected db state. Expected statisticsV2 to exist");
        }

        return result;
    }

    StatisticsV2Frame::pointer
    StatisticsV2Helper::mustLoadStatistics(AccountID& accountID, StatsOpType statsOpType, AssetCode const& assetCode,
                                           bool isConvertNeeded, Database &db, LedgerDelta *delta)
    {
        auto result = loadStatistics(accountID, statsOpType, assetCode, isConvertNeeded, db, delta);
        if (!result)
        {
            CLOG(ERROR, Logging::ENTRY_LOGGER)
                    << "Unexpected db state. Expected statisticsV2 to exists. accountID: "
                    << PubKeyUtils::toStrKey(accountID);
            throw std::runtime_error("Unexpected db state. Expected statisticsV2 to exist");
        }

        return result;
    }

}