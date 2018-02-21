#include "crypto/SecretKey.h"
#include "crypto/Hex.h"
#include "database/Database.h"
#include "LedgerDelta.h"
#include "ledger/LedgerManager.h"
#include "ledger/ExternalSystemAccountIDPoolEntryHelper.h"
#include "lib/util/format.h"
#include "xdrpp/printer.h"

using namespace soci;
using namespace std;

namespace stellar
{
using xdr::operator<;

    const char* ExternalSystemAccountIDPoolEntryHelper::select = "SELECT * FROM external_system_account_id_pool";

    void ExternalSystemAccountIDPoolEntryHelper::storeUpdateHelper(LedgerDelta &delta, Database &db, bool insert,
                                                                  LedgerEntry const &entry)
    {
        auto poolEntryFrame = make_shared<ExternalSystemAccountIDPoolEntryFrame>(entry);
        auto poolEntry = poolEntryFrame->getExternalSystemAccountIDPoolEntry();

        poolEntryFrame->touch(delta);

        poolEntryFrame->ensureValid();

        string sql;

        if (insert)
        {
            sql = "INSERT INTO external_system_account_id_pool (id, external_system_type, data, account_id, "
                    "expires_at, lastmodified, version) "
                    "VALUES (:id, :ex_sys_type, :data, :acc_id, :exp_at, :lm, :v)";
        }
        else
        {
            sql = "UPDATE external_system_account_id_pool "
                    "SET external_system_type = :ex_sys_type, data = :data, account_id = :acc_id, "
                    "expires_at = :exp_at, lastmodified = :lm, version = :v "
                    "WHERE id = :id";
        }

        auto prep = db.getPreparedStatement(sql);
        auto& st = prep.statement();

        st.exchange(use(poolEntry.poolEntryID, "id"));
        st.exchange(use(poolEntry.externalSystemType, "ex_sys_type"));
        st.exchange(use(poolEntry.data, "data"));

        std::string actIDStrKey;
        if(poolEntry.accountID)
        {
            actIDStrKey = PubKeyUtils::toStrKey(*poolEntry.accountID);
        }
        st.exchange(use(actIDStrKey, "acc_id"));

        st.exchange(use(poolEntry.expiresAt, "exp_at"));
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

    void ExternalSystemAccountIDPoolEntryHelper::storeAdd(LedgerDelta &delta, Database &db, LedgerEntry const &entry)
    {
        return storeUpdateHelper(delta, db, true, entry);
    }

    void ExternalSystemAccountIDPoolEntryHelper::storeChange(LedgerDelta &delta, Database &db, LedgerEntry const &entry)
    {
        return storeUpdateHelper(delta, db, false, entry);
    }

    void ExternalSystemAccountIDPoolEntryHelper::storeDelete(LedgerDelta &delta, Database &db, LedgerKey const &key)
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
    ExternalSystemAccountIDPoolEntryHelper::dropAll(Database &db)
    {
        db.getSession() << "DROP TABLE IF EXISTS external_system_account_id_pool;";
        db.getSession() << "CREATE TABLE external_system_account_id_pool"
            "("
            "id                   BIGINT      NOT NULL CHECK (id >= 0),"
            "external_system_type INT         NOT NULL,"
            "data                 TEXT        NOT NULL,"
            "account_id           VARCHAR(56) NOT NULL,"
            "expires_at           BIGINT      NOT NULL,"
            "lastmodified         INT         NOT NULL, "
            "version              INT         NOT NULL DEFAULT 0,"
            "PRIMARY KEY (id)"
            ");";
    }

    bool ExternalSystemAccountIDPoolEntryHelper::exists(Database &db, LedgerKey const &key)
    {
        auto const &poolEntry = key.externalSystemAccountIDPoolEntry();
        return exists(db, poolEntry.poolEntryID);
    }

    bool ExternalSystemAccountIDPoolEntryHelper::exists(Database &db, uint64_t poolEntryID)
    {
        int exists = 0;
        auto timer = db.getSelectTimer("external_system_account_id_pool-exists");
        auto prep = db.getPreparedStatement("SELECT EXISTS (SELECT NULL FROM external_system_account_id_pool WHERE "
                                            "id = :id)");
        auto& st = prep.statement();
        st.exchange(use(poolEntryID, "id"));
        st.exchange(into(exists));
        st.define_and_bind();
        st.execute(true);

        return exists != 0;
    }

    LedgerKey
    ExternalSystemAccountIDPoolEntryHelper::getLedgerKey(LedgerEntry const &from)
    {
        LedgerKey ledgerKey;
        ledgerKey.type(from.data.type());
        ledgerKey.externalSystemAccountIDPoolEntry().poolEntryID = from.data.externalSystemAccountIDPoolEntry().poolEntryID;
        return ledgerKey;
    }

    EntryFrame::pointer
    ExternalSystemAccountIDPoolEntryHelper::storeLoad(LedgerKey const &key, Database &db)
    {
        auto const &poolEntry = key.externalSystemAccountIDPoolEntry();
        return load(poolEntry.poolEntryID, db);
    }

    EntryFrame::pointer
    ExternalSystemAccountIDPoolEntryHelper::fromXDR(LedgerEntry const &from)
    {
        return std::make_shared<ExternalSystemAccountIDPoolEntryFrame>(from);
    }

    uint64_t ExternalSystemAccountIDPoolEntryHelper::countObjects(soci::session &sess)
    {
        uint64_t count = 0;
        sess << "SELECT COUNT(*) FROM external_system_account_id_pool;", into(count);
        return count;
    }

    ExternalSystemAccountIDPoolEntryFrame::pointer
    ExternalSystemAccountIDPoolEntryHelper::load(uint64_t poolEntryID, Database &db, LedgerDelta *delta)
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
    ExternalSystemAccountIDPoolEntryHelper::load(ExternalSystemType type, std::string const data, Database &db,
                                                LedgerDelta *delta)
    {
        string sql = select;
        sql += +" WHERE external_system_type = :ex_sys_type AND data = :data";
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

    void ExternalSystemAccountIDPoolEntryHelper::load(StatementContext &prep,
                                                     std::function<void(LedgerEntry const &)> processor)
    {
        LedgerEntry le;
        le.data.type(LedgerEntryType::EXTERNAL_SYSTEM_ACCOUNT_ID_POOL_ENTRY);
        auto& p = le.data.externalSystemAccountIDPoolEntry();
        int version;
        std::string actIDStrKey;

        statement& st = prep.statement();
        st.exchange(into(p.poolEntryID));
        st.exchange(into(p.externalSystemType));
        st.exchange(into(p.data));
        st.exchange(into(actIDStrKey));
        st.exchange(into(p.expiresAt));
        st.exchange(into(le.lastModifiedLedgerSeq));
        st.exchange(into(version));
        st.define_and_bind();
        st.execute(true);

        while (st.got_data())
        {
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

    std::vector<ExternalSystemAccountIDPoolEntryFrame::pointer>
    ExternalSystemAccountIDPoolEntryHelper::loadPool(Database &db)
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
