#include "crypto/SecretKey.h"
#include "crypto/Hex.h"
#include "database/Database.h"
#include "LedgerDelta.h"
#include "ledger/LedgerManager.h"
#include "ledger/ExternalSystemAccountIDPoolEntryHelperLegacy.h"
#include "lib/util/format.h"
#include "xdrpp/printer.h"

using namespace soci;
using namespace std;

namespace stellar
{
using xdr::operator<;

    const char* ExternalSystemAccountIDPoolEntryHelperLegacy::select = "SELECT id, external_system_type, data, parent, "
           "is_deleted, account_id, expires_at, binded_at, lastmodified, version FROM external_system_account_id_pool";

    void ExternalSystemAccountIDPoolEntryHelperLegacy::storeUpdateHelper(LedgerDelta &delta, Database &db, bool insert,
                                                                  LedgerEntry const &entry)
    {
        try
        {
            auto poolEntryFrame = make_shared<ExternalSystemAccountIDPoolEntryFrame>(entry);
            auto poolEntry = poolEntryFrame->getExternalSystemAccountIDPoolEntry();

            poolEntryFrame->touch(delta);

            poolEntryFrame->ensureValid();

            string sql;

            if (insert)
            {
                sql = "INSERT INTO external_system_account_id_pool (id, external_system_type, data, parent, "
                        "is_deleted, account_id, expires_at, binded_at, lastmodified, version) "
                        "VALUES (:id, :ex_sys_type, :data, :parent, :is_del, :acc_id, :exp_at, :bin_at, :lm, :v)";
            }
            else
            {
                sql = "UPDATE external_system_account_id_pool "
                        "SET external_system_type = :ex_sys_type, data = :data, parent = :parent, "
                        "is_deleted = :is_del, account_id = :acc_id, expires_at = :exp_at, binded_at = :bin_at, "
                        "lastmodified = :lm, version = :v "
                        "WHERE id = :id";
            }

            auto prep = db.getPreparedStatement(sql);
            auto& st = prep.statement();

            st.exchange(use(poolEntry.poolEntryID, "id"));
            st.exchange(use(poolEntry.externalSystemType, "ex_sys_type"));
            st.exchange(use(poolEntry.data, "data"));
            st.exchange(use(poolEntry.parent, "parent"));
            int isDeleted = poolEntry.isDeleted ? 1 : 0;
            st.exchange(use(isDeleted, "is_del"));

            std::string actIDStrKey;
            if(poolEntry.accountID)
            {
                actIDStrKey = PubKeyUtils::toStrKey(*poolEntry.accountID);
            }
            st.exchange(use(actIDStrKey, "acc_id"));

            st.exchange(use(poolEntry.expiresAt, "exp_at"));
            st.exchange(use(poolEntry.bindedAt, "bin_at"));
            st.exchange(use(poolEntryFrame->mEntry.lastModifiedLedgerSeq, "lm"));

            const auto version = static_cast<int32_t>(poolEntry.ext.v());
            st.exchange(use(version, "v"));
            st.define_and_bind();

            auto timer = insert ? db.getInsertTimer("external_system_account_id_pool")
                                : db.getUpdateTimer("external_system_account_id_pool");
            st.execute(true);

            if (st.get_affected_rows() != 1)
            {
                throw runtime_error("could not update SQL");
            }

            if (insert)
            {
                delta.addEntry(*poolEntryFrame);
            }
            else
            {
                delta.modEntry(*poolEntryFrame);
            }
        }
        catch (exception ex)
        {
            CLOG(ERROR, Logging::ENTRY_LOGGER) << "Failed to update external system account id pool entry"
                                               << xdr::xdr_to_string(entry) << " reason: " << ex.what();
            throw_with_nested(runtime_error("Failed to update external system account id pool entry"));
        }
    }

    void ExternalSystemAccountIDPoolEntryHelperLegacy::storeAdd(LedgerDelta &delta, Database &db, LedgerEntry const &entry)
    {
        return storeUpdateHelper(delta, db, true, entry);
    }

    void ExternalSystemAccountIDPoolEntryHelperLegacy::storeChange(LedgerDelta &delta, Database &db, LedgerEntry const &entry)
    {
        return storeUpdateHelper(delta, db, false, entry);
    }

    void ExternalSystemAccountIDPoolEntryHelperLegacy::storeDelete(LedgerDelta &delta, Database &db, LedgerKey const &key)
    {
        auto timer = db.getDeleteTimer("external_system_account_id_pool");
        auto prep = db.getPreparedStatement("DELETE FROM external_system_account_id_pool WHERE id = :id");
        auto& st = prep.statement();
        const auto &poolEntry = key.externalSystemAccountIDPoolEntry();
        st.exchange(use(poolEntry.poolEntryID, "id"));
        st.define_and_bind();
        st.execute(true);
        delta.deleteEntry(key);
    }

    void
    ExternalSystemAccountIDPoolEntryHelperLegacy::dropAll(Database &db)
    {
        db.getSession() << "DROP TABLE IF EXISTS external_system_account_id_pool;";
        db.getSession() << "CREATE TABLE external_system_account_id_pool"
            "("
            "id                   BIGINT      NOT NULL CHECK (id >= 0),"
            "external_system_type INT         NOT NULL,"
            "data                 TEXT        NOT NULL,"
            "parent               INT         NOT NULL,"
            "is_deleted           BOOLEAN     NOT NULL,"
            "account_id           VARCHAR(56) NOT NULL,"
            "expires_at           BIGINT      NOT NULL,"
            "binded_at            BIGINT      NOT NULL,"
            "lastmodified         INT         NOT NULL, "
            "version              INT         NOT NULL DEFAULT 0,"
            "PRIMARY KEY (id)"
            ");";

        fixTypes(db);
    }

    void ExternalSystemAccountIDPoolEntryHelperLegacy::fixTypes(Database & db)
    {
        db.getSession() << "ALTER TABLE external_system_account_id_pool ALTER parent SET DATA TYPE BIGINT;";
        db.getSession() << "ALTER TABLE external_system_account_id_pool ALTER external_system_type SET DATA TYPE BIGINT;";
    }

    void ExternalSystemAccountIDPoolEntryHelperLegacy::parentToNumeric(Database & db)
    {
        db.getSession() << "ALTER TABLE external_system_account_id_pool ALTER parent SET DATA TYPE NUMERIC(20, 0);";
    }

    bool ExternalSystemAccountIDPoolEntryHelperLegacy::exists(Database &db, LedgerKey const &key)
    {
        auto const &poolEntry = key.externalSystemAccountIDPoolEntry();
        return exists(db, poolEntry.poolEntryID);
    }

    bool ExternalSystemAccountIDPoolEntryHelperLegacy::exists(Database &db, uint64_t poolEntryID)
    {
        int exists = 0;
        auto timer = db.getSelectTimer("external_system_account_id_pool_exists");
        auto prep = db.getPreparedStatement("SELECT EXISTS (SELECT NULL FROM external_system_account_id_pool WHERE "
                                            "id = :id)");
        auto& st = prep.statement();
        st.exchange(use(poolEntryID, "id"));
        st.exchange(into(exists));
        st.define_and_bind();
        st.execute(true);

        return exists != 0;
    }

    bool ExternalSystemAccountIDPoolEntryHelperLegacy::existsForAccount(Database &db, int32 externalSystemType,
                                                        AccountID accountID) {
        int exists = 0;
        auto timer = db.getSelectTimer("external_system_account_id_pool_exists");
        auto prep = db.getPreparedStatement("SELECT EXISTS (SELECT NULL FROM external_system_account_id_pool WHERE "
                                                    "external_system_type = :ex_sys_type AND account_id = :aid)");
        auto& st = prep.statement();
        st.exchange(use(externalSystemType, "ex_sys_type"));

        string actIDStrKey = PubKeyUtils::toStrKey(accountID);
        st.exchange(use(actIDStrKey, "aid"));

        st.exchange(into(exists));
        st.define_and_bind();
        st.execute(true);

        return exists != 0;
    }

    LedgerKey
    ExternalSystemAccountIDPoolEntryHelperLegacy::getLedgerKey(LedgerEntry const &from)
    {
        LedgerKey ledgerKey;
        ledgerKey.type(from.data.type());
        ledgerKey.externalSystemAccountIDPoolEntry().poolEntryID = from.data.externalSystemAccountIDPoolEntry().poolEntryID;
        return ledgerKey;
    }

    EntryFrame::pointer
    ExternalSystemAccountIDPoolEntryHelperLegacy::storeLoad(LedgerKey const &key, Database &db)
    {
        auto const &poolEntry = key.externalSystemAccountIDPoolEntry();
        return load(poolEntry.poolEntryID, db);
    }

    EntryFrame::pointer
    ExternalSystemAccountIDPoolEntryHelperLegacy::fromXDR(LedgerEntry const &from)
    {
        return std::make_shared<ExternalSystemAccountIDPoolEntryFrame>(from);
    }

    uint64_t ExternalSystemAccountIDPoolEntryHelperLegacy::countObjects(soci::session &sess)
    {
        uint64_t count = 0;
        sess << "SELECT COUNT(*) FROM external_system_account_id_pool;", into(count);
        return count;
    }

    ExternalSystemAccountIDPoolEntryFrame::pointer
    ExternalSystemAccountIDPoolEntryHelperLegacy::load(uint64_t poolEntryID, Database &db, LedgerDelta *delta)
    {
        string sql = select;
        sql += +" WHERE id = :id";
        auto prep = db.getPreparedStatement(sql);
        auto& st = prep.statement();
        st.exchange(use(poolEntryID, "id"));

        ExternalSystemAccountIDPoolEntryFrame::pointer result;
        auto timer = db.getSelectTimer("external_system_account_id_pool");
        load(prep, [&result](LedgerEntry const& entry)
        {
            result = make_shared<ExternalSystemAccountIDPoolEntryFrame>(entry);
        });

        if (!result)
        {
            return nullptr;
        }

        if (delta)
        {
            delta->recordEntry(*result);
        }

        return result;
    }

    ExternalSystemAccountIDPoolEntryFrame::pointer
    ExternalSystemAccountIDPoolEntryHelperLegacy::load(int32 type, std::string const data, Database &db, LedgerDelta *delta)
    {
        string sql = select;
        sql += +" WHERE external_system_type = :ex_sys_type AND data = :data ";
        auto prep = db.getPreparedStatement(sql);
        auto& st = prep.statement();
        st.exchange(use(type, "ex_sys_type"));
        st.exchange(use(data, "data"));

        ExternalSystemAccountIDPoolEntryFrame::pointer result;
        auto timer = db.getSelectTimer("external_system_account_id_pool");
        load(prep, [&result](LedgerEntry const& entry)
        {
            result = make_shared<ExternalSystemAccountIDPoolEntryFrame>(entry);
        });

        if (!result)
        {
            return nullptr;
        }

        if (delta)
        {
            delta->recordEntry(*result);
        }

        return result;
    }

    ExternalSystemAccountIDPoolEntryFrame::pointer
    ExternalSystemAccountIDPoolEntryHelperLegacy::load(int32 externalSystemType, AccountID accountID,
                                                 Database &db, LedgerDelta *delta)
    {
        string sql = select;
        sql += +" WHERE external_system_type = :ex_sys_type AND account_id = :aid";
        auto prep = db.getPreparedStatement(sql);
        auto& st = prep.statement();
        st.exchange(use(externalSystemType, "ex_sys_type"));

        string actIDStrKey = PubKeyUtils::toStrKey(accountID);
        st.exchange(use(actIDStrKey, "aid"));

        ExternalSystemAccountIDPoolEntryFrame::pointer result;
        auto timer = db.getSelectTimer("external_system_account_id_pool");
        load(prep, [&result](LedgerEntry const& entry)
        {
            result = make_shared<ExternalSystemAccountIDPoolEntryFrame>(entry);
        });

        if (!result)
        {
            return nullptr;
        }

        if (delta)
        {
            delta->recordEntry(*result);
        }

        return result;
    }

    void ExternalSystemAccountIDPoolEntryHelperLegacy::load(StatementContext &prep,
                                                     std::function<void(LedgerEntry const &)> processor)
    {
        try
        {
            int isDeleted;
            LedgerEntry le;
            le.data.type(LedgerEntryType::EXTERNAL_SYSTEM_ACCOUNT_ID_POOL_ENTRY);
            auto& p = le.data.externalSystemAccountIDPoolEntry();
            int version;
            std::string actIDStrKey;

            statement& st = prep.statement();
            st.exchange(into(p.poolEntryID));
            st.exchange(into(p.externalSystemType));
            st.exchange(into(p.data));
            st.exchange(into(p.parent));
            st.exchange(into(isDeleted));
            st.exchange(into(actIDStrKey));
            st.exchange(into(p.expiresAt));
            st.exchange(into(p.bindedAt));
            st.exchange(into(le.lastModifiedLedgerSeq));
            st.exchange(into(version));
            st.define_and_bind();
            st.execute(true);

            while (st.got_data())
            {
                p.isDeleted = isDeleted > 0;
                p.ext.v(static_cast<LedgerVersion>(version));

                if (!actIDStrKey.empty())
                {
                    p.accountID.activate() = PubKeyUtils::fromStrKey(actIDStrKey);
                }

                ExternalSystemAccountIDPoolEntryFrame::ensureValid(p);
                processor(le);
                st.fetch();
            }
        }
        catch (...)
        {
            throw_with_nested(runtime_error("Failed to load external system account id pool entry"));
        }
    }

    ExternalSystemAccountIDPoolEntryFrame::pointer
    ExternalSystemAccountIDPoolEntryHelperLegacy::loadAvailablePoolEntry(Database &db, LedgerManager &ledgerManager,
                                                                   int32 externalSystemType)
    {
        string sql = select;
        sql += +" WHERE external_system_type = :ex_sys_type AND expires_at < :time AND is_deleted = FALSE "
                "ORDER BY account_id = '' DESC, expires_at, id LIMIT 1";
        auto prep = db.getPreparedStatement(sql);
        auto& st = prep.statement();
        st.exchange(use(externalSystemType, "ex_sys_type"));

        uint64_t time = ledgerManager.getCloseTime();
        st.exchange(use(time, "time"));

        ExternalSystemAccountIDPoolEntryFrame::pointer result;
        auto timer = db.getSelectTimer("external system account id pool");
        load(prep, [&result](LedgerEntry const& entry)
        {
            result = make_shared<ExternalSystemAccountIDPoolEntryFrame>(entry);
        });

        if (!result)
        {
            return nullptr;
        }

        return result;
    }

    std::vector<ExternalSystemAccountIDPoolEntryFrame::pointer>
    ExternalSystemAccountIDPoolEntryHelperLegacy::loadPool(Database &db)
    {
        std::vector<ExternalSystemAccountIDPoolEntryFrame::pointer> retPool;
        std::string sql = select;
        auto prep = db.getPreparedStatement(sql);

        auto timer = db.getSelectTimer("external system account id pool");
        load(prep, [&retPool](LedgerEntry const& of)
        {
            retPool.emplace_back(make_shared<ExternalSystemAccountIDPoolEntryFrame>(of));
        });
        return retPool;
    }
}
