#include "crypto/SecretKey.h"
#include "crypto/Hex.h"
#include "database/Database.h"
#include "LedgerDelta.h"
#include "ledger/LedgerManager.h"
#include "ledger/ExternalSystemAccountIDProviderHelper.h"
#include "lib/util/format.h"
#include "xdrpp/printer.h"

using namespace soci;
using namespace std;

namespace stellar
{
using xdr::operator<;

    const char* ExternalSystemAccountIDProviderHelper::select = "SELECT * FROM external_system_account_id_pool";

    void ExternalSystemAccountIDProviderHelper::storeUpdateHelper(LedgerDelta &delta, Database &db, bool insert,
                                                                  LedgerEntry const &entry)
    {
        auto providerFrame = make_shared<ExternalSystemAccountIDProviderFrame>(entry);
        auto providerEntry = providerFrame->getExternalSystemAccountIDProvider();

        providerFrame->touch(delta);

        bool isValid = providerFrame->isValid();
        if (!isValid)
        {
            CLOG(ERROR, Logging::ENTRY_LOGGER)
                    << "Unexpected state: trying to insert/update invalid external system account id provider: "
                    << xdr::xdr_to_string(providerEntry);
            throw runtime_error("Unexpected state: invalid external system account id provider");
        }

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

        st.exchange(use(providerEntry.providerID, "id"));
        st.exchange(use(providerEntry.externalSystemType, "ex_sys_type"));
        st.exchange(use(providerEntry.data, "data"));

        std::string actIDStrKey;
        if(providerEntry.accountID)
        {
            actIDStrKey = PubKeyUtils::toStrKey(*providerEntry.accountID);
        }
        st.exchange(use(actIDStrKey, "acc_id"));

        st.exchange(use(providerEntry.expiresAt, "exp_at"));
        st.exchange(use(providerFrame->mEntry.lastModifiedLedgerSeq, "lm"));

        const auto version = static_cast<int32_t>(providerEntry.ext.v());
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
            delta.addEntry(*providerFrame);
        }
        else
        {
            delta.modEntry(*providerFrame);
        }
    }

    void ExternalSystemAccountIDProviderHelper::storeAdd(LedgerDelta &delta, Database &db, LedgerEntry const &entry)
    {
        return storeUpdateHelper(delta, db, true, entry);
    }

    void ExternalSystemAccountIDProviderHelper::storeChange(LedgerDelta &delta, Database &db, LedgerEntry const &entry)
    {
        return storeUpdateHelper(delta, db, false, entry);
    }

    void ExternalSystemAccountIDProviderHelper::storeDelete(LedgerDelta &delta, Database &db, LedgerKey const &key)
    {
        auto timer = db.getDeleteTimer("external_system_account_id_pool");
        auto prep = db.getPreparedStatement("DELETE FROM external_system_account_id_pool WHERE id = :id");
        auto& st = prep.statement();
        const auto &provider = key.externalSystemAccountIDProvider();
        st.exchange(use(provider.providerID, "id"));
        st.define_and_bind();
        st.execute(true);
        delta.deleteEntry(key);
    }

    void
    ExternalSystemAccountIDProviderHelper::dropAll(Database &db)
    {
        db.getSession() << "DROP TABLE IF EXISTS external_system_account_id_pool;";
        db.getSession() << "CREATE TABLE external_system_account_id_pool"
            "("
            "id                   BIGINT      NOT NULL CHECK (id >= 0),"
            "external_system_type INT         NOT NULL,"
            "data                 TEXT        NOT NULL,"
            "account_id           VARCHAR(56),"
            "expires_at           BIGINT      NOT NULL,"
            "lastmodified         INT         NOT NULL, "
            "version              INT         NOT NULL DEFAULT 0,"
            "PRIMARY KEY (id)"
            ");";
    }

    bool ExternalSystemAccountIDProviderHelper::exists(Database &db, LedgerKey const &key)
    {
        auto const &provider = key.externalSystemAccountIDProvider();
        return exists(db, provider.providerID);
    }

    bool ExternalSystemAccountIDProviderHelper::exists(Database &db, uint64_t providerID)
    {
        int exists = 0;
        auto timer = db.getSelectTimer("external_system_account_id_pool-exists");
        auto prep = db.getPreparedStatement("SELECT EXISTS (SELECT NULL FROM external_system_account_id_pool WHERE "
                                            "id = :id)");
        auto& st = prep.statement();
        st.exchange(use(providerID, "id"));
        st.exchange(into(exists));
        st.define_and_bind();
        st.execute(true);

        return exists != 0;
    }

    LedgerKey
    ExternalSystemAccountIDProviderHelper::getLedgerKey(LedgerEntry const &from)
    {
        LedgerKey ledgerKey;
        ledgerKey.type(from.data.type());
        ledgerKey.externalSystemAccountIDProvider().providerID = from.data.externalSystemAccountIDProvider().providerID;
        return ledgerKey;
    }

    EntryFrame::pointer
    ExternalSystemAccountIDProviderHelper::storeLoad(LedgerKey const &key, Database &db)
    {
        auto const &provider = key.externalSystemAccountIDProvider();
        return load(provider.providerID, db);
    }

    EntryFrame::pointer
    ExternalSystemAccountIDProviderHelper::fromXDR(LedgerEntry const &from)
    {
        return std::make_shared<ExternalSystemAccountIDProviderFrame>(from);
    }

    uint64_t ExternalSystemAccountIDProviderHelper::countObjects(soci::session &sess)
    {
        uint64_t count = 0;
        sess << "SELECT COUNT(*) FROM external_system_account_id_pool;", into(count);
        return count;
    }

    ExternalSystemAccountIDProviderFrame::pointer
    ExternalSystemAccountIDProviderHelper::load(uint64_t providerID, Database &db, LedgerDelta *delta)
    {
        string sql = select;
        sql += +" WHERE id = :id";
        auto prep = db.getPreparedStatement(sql);
        auto& st = prep.statement();
        st.exchange(use(providerID, "id"));

        ExternalSystemAccountIDProviderFrame::pointer result;
        auto timer = db.getSelectTimer("external_system_account_id_pool");
        load(prep, [&result](LedgerEntry const& entry)
        {
            result = make_shared<ExternalSystemAccountIDProviderFrame>(entry);
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

    ExternalSystemAccountIDProviderFrame::pointer
    ExternalSystemAccountIDProviderHelper::load(ExternalSystemType type, std::string const data, Database &db,
                                                LedgerDelta *delta)
    {
        string sql = select;
        sql += +" WHERE external_system_type = :ex_sys_type AND data = :data";
        auto prep = db.getPreparedStatement(sql);
        auto& st = prep.statement();
        st.exchange(use(type, "ex_sys_type"));
        st.exchange(use(data, "data"));

        ExternalSystemAccountIDProviderFrame::pointer result;
        auto timer = db.getSelectTimer("external_system_account_id_pool");
        load(prep, [&result](LedgerEntry const& entry)
        {
            result = make_shared<ExternalSystemAccountIDProviderFrame>(entry);
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

    void ExternalSystemAccountIDProviderHelper::load(StatementContext &prep,
                                                     std::function<void(LedgerEntry const &)> processor)
    {
        LedgerEntry le;
        le.data.type(LedgerEntryType::EXTERNAL_SYSTEM_ACCOUNT_ID_PROVIDER);
        auto& p = le.data.externalSystemAccountIDProvider();
        int version;
        std::string actIDStrKey;

        statement& st = prep.statement();
        st.exchange(into(p.providerID));
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

            bool isValid = ExternalSystemAccountIDProviderFrame::isValid(p);
            if (!isValid)
            {
                CLOG(ERROR, Logging::ENTRY_LOGGER)
                        << "Unexpected state: loaded invalid external system account id provider: "
                        << xdr::xdr_to_string(p);
                throw runtime_error("Loaded invalid external system account id provider");
            }

            processor(le);
            st.fetch();
        }
    }

    std::vector<ExternalSystemAccountIDProviderFrame::pointer>
    ExternalSystemAccountIDProviderHelper::loadPool(Database &db)
    {
        std::vector<ExternalSystemAccountIDProviderFrame::pointer> retPool;
        std::string sql = select;
        auto prep = db.getPreparedStatement(sql);

        auto timer = db.getSelectTimer("external system account id pool");
        load(prep, [&retPool](LedgerEntry const& of)
        {
            retPool.emplace_back(make_shared<ExternalSystemAccountIDProviderFrame>(of));
        });
        return retPool;
    }
}
