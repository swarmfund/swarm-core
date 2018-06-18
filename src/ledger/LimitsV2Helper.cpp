//
// Created by artem on 29.05.18.
//

#include <search.h>
#include <lib/xdrpp/xdrpp/printer.h>
#include "LimitsV2Helper.h"
#include "LimitsV2Frame.h"
#include "crypto/SecretKey.h"
#include "LedgerDelta.h"
#include "AccountFrame.h"
#include "BalanceFrame.h"

using namespace std;
using namespace soci;

namespace  stellar
{
    const char* limitsV2Selector = "select limits_v2_id, account_type, account_id, stats_op_type, asset_code,"
                                   " is_convert_needed, daily_out, weekly_out, monthly_out, annual_out,"
                                   " lastmodified, version "
                                   "from limits_v2 ";

    void LimitsV2Helper::dropAll(Database &db)
    {
        db.getSession() << "DROP TABLE IF EXISTS limits_v2;";
        db.getSession() << "CREATE TABLE limits_v2"
                   "("
                   "limits_v2_id        BIGINT      NOT NULL CHECK (limits_v2_id >= 0),"
                   "account_type        INT         ,"
                   "account_id          VARCHAR(56) ,"
                   "stats_op_type       INT         NOT NULL,"
                   "asset_code          TEXT        NOT NULL,"
                   "is_convert_needed   BOOLEAN     NOT NULL,"
                   "daily_out           BIGINT      NOT NULL,"
                   "weekly_out          BIGINT      NOT NULL,"
                   "monthly_out         BIGINT      NOT NULL,"
                   "annual_out          BIGINT      NOT NULL,"
                   "lastmodified        INT         NOT NULL,"
                   "version             INT         NOT NULL DEFAULT 0,"
                   "CONSTRAINT limits_v2_id UNIQUE (account_type, account_id, stats_op_type, "
                   "                                asset_code, is_convert_needed)"
                   ");";
    }

    void
    LimitsV2Helper::storeUpdateHelper(LedgerDelta &delta, Database &db, bool insert, LedgerEntry const &entry)
    {
        auto limitsV2Frame = make_shared<LimitsV2Frame>(entry);
        auto limitsV2Entry = limitsV2Frame->getLimits();

        limitsV2Frame->touch(delta);

        auto key = limitsV2Frame->getKey();
        flushCachedEntry(key, db);

        if (!limitsV2Frame->isValid())
        {
            CLOG(ERROR, Logging::ENTRY_LOGGER) << "Unexpected state - limitsV2 is invalid: "
                                               << xdr::xdr_to_string(limitsV2Entry);
            throw runtime_error("Invalid limitsV2 state");
        }

        string sql;

        if (insert)
        {
            sql = "INSERT INTO limits_v2 (limits_v2_id, account_type, account_id, stats_op_type, asset_code, "
                  "is_convert_needed, daily_out, weekly_out, monthly_out, annual_out, lastmodified, version) "
                  "VALUES (:id, :acc_t, :acc_id, :stats_t, :asset_c, :is_c, :d_o, :w_o, :m_o, :a_o, :lm, :v)";
        } else
        {
            sql = "UPDATE limits_v2 "
                  "SET    account_type=:acc_t, account_id=:acc_id, stats_op_type=:stats_t, asset_code=:asset_c, "
                  "       is_convert_needed=:is_c, daily_out=:d_o, weekly_out=:w_o, monthly_out=:m_o, annual_out=:a_o, "
                  "       lastmodified=:lm, version=:v "
                  "WHERE  limits_v2_id = :id";
        }

        auto prep = db.getPreparedStatement(sql);
        auto& st = prep.statement();

        int32_t accountType = 0;
        if (!!limitsV2Entry.accountType)
            accountType = static_cast<int32_t>(*limitsV2Entry.accountType);//check it

        string accountIDStr;
        if (!!limitsV2Entry.accountID)
            accountIDStr = PubKeyUtils::toStrKey(*limitsV2Entry.accountID);//check it

        auto statsOpType = static_cast<int32_t>(limitsV2Entry.statsOpType);
        int isConvertNeeded = limitsV2Entry.isConvertNeeded ? 1 : 0;
        auto version = static_cast<int32_t>(limitsV2Entry.ext.v());

        st.exchange(use(limitsV2Entry.id, "id"));
        st.exchange(use(accountType, "acc_t"));
        st.exchange(use(accountIDStr, "acc_id"));
        st.exchange(use(statsOpType, "stats_t"));
        st.exchange(use(limitsV2Entry.assetCode, "asset_c"));
        st.exchange(use(isConvertNeeded, "is_c"));
        st.exchange(use(limitsV2Entry.dailyOut, "d_o"));
        st.exchange(use(limitsV2Entry.weeklyOut, "w_o"));
        st.exchange(use(limitsV2Entry.monthlyOut, "m_o"));
        st.exchange(use(limitsV2Entry.annualOut, "a_o"));
        st.exchange(use(limitsV2Frame->mEntry.lastModifiedLedgerSeq, "lm"));
        st.exchange(use(version, "v"));

        st.define_and_bind();

        auto timer = insert ? db.getInsertTimer("limits-v2")
                            : db.getUpdateTimer("limits-v2");
        st.execute(true);

        if (st.get_affected_rows() != 1)
        {
            throw runtime_error("could not update SQL");
        }

        if (insert)
        {
            delta.addEntry(*limitsV2Frame);
        }
        else
        {
            delta.modEntry(*limitsV2Frame);
        }
    }

    void
    LimitsV2Helper::storeAdd(LedgerDelta& delta, Database& db, LedgerEntry const& entry)
    {
        storeUpdateHelper(delta, db, true, entry);
    }

    void
    LimitsV2Helper::storeChange(LedgerDelta& delta, Database& db, LedgerEntry const& entry)
    {
        storeUpdateHelper(delta, db, false, entry);
    }

    void
    LimitsV2Helper::storeDelete(LedgerDelta& delta, Database& db, LedgerKey const& key)
    {
        auto timer = db.getDeleteTimer("limits-v2");
        auto prep = db.getPreparedStatement("DELETE FROM limits_v2 WHERE limits_v2_id = :id");
        auto& st = prep.statement();
        st.exchange(use(key.limitsV2().id, "id"));
        st.define_and_bind();
        st.execute(true);
        delta.deleteEntry(key);
    }

    bool
    LimitsV2Helper::exists(Database& db, LedgerKey const& key)
    {
        int exists = 0;
        auto timer = db.getSelectTimer("limits_v2_exists");
        auto prep = db.getPreparedStatement("SELECT EXISTS (SELECT NULL FROM limits_v2 WHERE limits_v2_id = :id)");
        auto& st = prep.statement();
        st.exchange(use(key.limitsV2().id, "id"));
        st.exchange(into(exists));
        st.define_and_bind();
        st.execute(true);

        return exists != 0;
    }

    LedgerKey
    LimitsV2Helper::getLedgerKey(LedgerEntry const& from)
    {
        LedgerKey ledgerKey;
        ledgerKey.type(from.data.type());
        ledgerKey.limitsV2().id = from.data.limitsV2().id;
        return ledgerKey;
    }

    EntryFrame::pointer
    LimitsV2Helper::fromXDR(LedgerEntry const &from)
    {
        return std::make_shared<LimitsV2Frame>(from);
    }

    uint64_t
    LimitsV2Helper::countObjects(soci::session &sess)
    {
        uint64_t count = 0;
        sess << "SELECT COUNT(*) FROM limits_v2;", into(count);
        return count;
    }

    EntryFrame::pointer
    LimitsV2Helper::storeLoad(LedgerKey const &key, Database &db)
    {
        auto const &limitsV2Entry = key.limitsV2();
        return loadLimits(limitsV2Entry.id, db);
    }

    LimitsV2Frame::pointer
    LimitsV2Helper::loadLimits(uint64_t id, Database &db, LedgerDelta *delta)
    {
        string sql = limitsV2Selector;
        sql += " where limits_v2_id = :id";

        auto prep = db.getPreparedStatement(sql);
        auto& st = prep.statement();
        st.exchange(use(id, "id"));

        LimitsV2Frame::pointer result;
        auto timer = db.getSelectTimer("limits-v2");
        load(prep, [&result](LedgerEntry const& entry)
        {
            result = make_shared<LimitsV2Frame>(entry);
        });

        if (!result)
            return nullptr;

        if (delta)
            delta->recordEntry(*result);

        return result;
    }

    std::vector<LimitsV2Frame::pointer>
    LimitsV2Helper::loadLimits(Database &db, StatsOpType statsOpType, AssetCode assetCode,
                               xdr::pointer<AccountID> accountID, xdr::pointer<AccountType> accountType,
                               LedgerDelta *delta)
    {
        string accountIDStr;
        if (!!accountID)
            accountIDStr = PubKeyUtils::toStrKey(*accountID);

        int32_t accountTypeInt = 0;
        if (!!accountType)
            accountTypeInt = static_cast<int32_t>(*accountType);

        auto statsOpTypeInt = static_cast<int32_t>(statsOpType);
        auto spendOpTypeInt = static_cast<int32_t>(StatsOpType::SPEND);

        string sql = "select distinct on (stats_op_type, asset_code, is_convert_needed)  limits_v2_id, "
                     "account_type, account_id, stats_op_type, asset_code, is_convert_needed, daily_out, "
                     "weekly_out, monthly_out, annual_out, lastmodified, version  "
                     "from limits_v2 "
                     "where (account_type=:acc_t or account_type = 0) and (account_id=:acc_id or account_id is null)"
                     " and  (asset_code=:asset_c or is_convert_needed) and (stats_op_type in (:stats_t, :spend_t)) "
                     "order by stats_op_type, asset_code, is_convert_needed, account_id = :acc_id, "
                     "account_type = :acc_t desc;";

        auto prep = db.getPreparedStatement(sql);
        auto& st = prep.statement();
        st.exchange(use(accountIDStr, "acc_id"));
        st.exchange(use(accountTypeInt, "acc_t"));
        st.exchange(use(assetCode, "asset_c"));
        st.exchange(use(statsOpTypeInt, "stats_t"));
        st.exchange(use(spendOpTypeInt, "spend_t"));

        std::vector<LimitsV2Frame::pointer> result;
        auto timer = db.getSelectTimer("limits-v2");
        load(prep, [&result](LedgerEntry const& entry)
        {
            result.emplace_back(make_shared<LimitsV2Frame>(entry));
        });

        return result;
    }

    LimitsV2Frame::pointer
    LimitsV2Helper::loadLimits(Database &db, StatsOpType statsOpType, AssetCode assetCode,
                               xdr::pointer<AccountID> accountID, xdr::pointer<AccountType> accountType,
                               bool isConvertNeeded, LedgerDelta *delta)
    {
        string accountIDStr;
        if (!!accountID)
            accountIDStr = PubKeyUtils::toStrKey(*accountID);

        int32_t accountTypeInt = 0;
        if (!!accountType)
            accountTypeInt = static_cast<int32_t>(*accountType);

        auto statsOpTypeInt = static_cast<int32_t>(statsOpType);
        int isConvertNeededInt = isConvertNeeded ? 1 : 0;

        string sql = limitsV2Selector;
        sql += " WHERE account_id = :acc_id AND account_type = :acc_t AND asset_code = :asset_c AND"
               "       stats_op_type = :stats_t AND is_convert_needed = :is_c; ";

        auto prep = db.getPreparedStatement(sql);
        auto& st = prep.statement();
        st.exchange(use(accountIDStr, "acc_id"));
        st.exchange(use(accountTypeInt, "acc_t"));
        st.exchange(use(assetCode, "asset_c"));
        st.exchange(use(statsOpTypeInt, "stats_t"));
        st.exchange(use(isConvertNeededInt, "is_c"));

        LimitsV2Frame::pointer result;
        auto timer = db.getSelectTimer("limits-v2");
        load(prep, [&result](LedgerEntry const& entry)
        {
            result = make_shared<LimitsV2Frame>(entry);
        });

        if (!result)
            return nullptr;

        if (delta)
            delta->recordEntry(*result);

        return result;
    }

    void
    LimitsV2Helper::load(StatementContext &prep, function<void(LedgerEntry const &)> processor)
    {
        try
        {
            LedgerEntry le;
            le.data.type(LedgerEntryType::LIMITS_V2);
            auto& limitsV2 = le.data.limitsV2();

            int32_t accountType;
            string accountIDStr;
            int32_t statsOpType;
            int32_t isConvertNeeded;
            int32_t version;

            auto& st = prep.statement();
            st.exchange(into(limitsV2.id));
            st.exchange(into(accountType));
            st.exchange(into(accountIDStr));
            st.exchange(into(statsOpType));
            st.exchange(into(limitsV2.assetCode));
            st.exchange(into(isConvertNeeded));
            st.exchange(into(limitsV2.dailyOut));
            st.exchange(into(limitsV2.weeklyOut));
            st.exchange(into(limitsV2.monthlyOut));
            st.exchange(into(limitsV2.annualOut));
            st.exchange(into(le.lastModifiedLedgerSeq));
            st.exchange(into(version));
            st.define_and_bind();
            st.execute(true);

            while (st.got_data())
            {
                if (accountType != 0)
                    limitsV2.accountType.activate() = static_cast<AccountType>(accountType);

                if (!accountIDStr.empty())
                    limitsV2.accountID.activate() = PubKeyUtils::fromStrKey(accountIDStr);

                limitsV2.statsOpType = static_cast<StatsOpType>(statsOpType);
                limitsV2.isConvertNeeded = isConvertNeeded > 0;
                limitsV2.ext.v(static_cast<LedgerVersion>(version));

                processor(le);
                st.fetch();
            }
        }
        catch (...)
        {
            throw_with_nested(runtime_error("Failed to load limits v2"));
        }
    }

}