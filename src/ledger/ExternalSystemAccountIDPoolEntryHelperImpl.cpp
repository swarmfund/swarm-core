#include "ledger/ExternalSystemAccountIDPoolEntryHelperImpl.h"
#include "LedgerDelta.h"
#include "crypto/Hex.h"
#include "crypto/SecretKey.h"
#include "database/Database.h"
#include "ledger/LedgerManager.h"
#include "ledger/StorageHelper.h"
#include "lib/util/format.h"
#include "xdrpp/printer.h"

using namespace soci;

namespace stellar
{
using xdr::operator<;

const char* ExternalSystemAccountIDPoolEntryHelperImpl::select =
    "SELECT id, external_system_type, data, parent, "
    "is_deleted, account_id, expires_at, binded_at, lastmodified, version FROM "
    "external_system_account_id_pool";

ExternalSystemAccountIDPoolEntryHelperImpl::
    ExternalSystemAccountIDPoolEntryHelperImpl(StorageHelper& storageHelper)
    : mStorageHelper(storageHelper)
{
}

void
ExternalSystemAccountIDPoolEntryHelperImpl::storeUpdateHelper(
    bool insert, LedgerEntry const& entry)
{
    try
    {
        auto poolEntryFrame =
            std::make_shared<ExternalSystemAccountIDPoolEntryFrame>(entry);
        auto poolEntry = poolEntryFrame->getExternalSystemAccountIDPoolEntry();

        poolEntryFrame->touch(mStorageHelper.getLedgerDelta());

        poolEntryFrame->ensureValid();

        std::string sql;

        if (insert)
        {
            sql = "INSERT INTO external_system_account_id_pool (id, "
                  "external_system_type, data, parent, "
                  "is_deleted, account_id, expires_at, binded_at, "
                  "lastmodified, version) "
                  "VALUES (:id, :ex_sys_type, :data, :parent, :is_del, "
                  ":acc_id, :exp_at, :bin_at, :lm, :v)";
        }
        else
        {
            sql = "UPDATE external_system_account_id_pool "
                  "SET external_system_type = :ex_sys_type, data = :data, "
                  "parent = :parent, "
                  "is_deleted = :is_del, account_id = :acc_id, expires_at = "
                  ":exp_at, binded_at = :bin_at, "
                  "lastmodified = :lm, version = :v "
                  "WHERE id = :id";
        }

        auto prep = getDatabase().getPreparedStatement(sql);
        auto& st = prep.statement();

        st.exchange(use(poolEntry.poolEntryID, "id"));
        st.exchange(use(poolEntry.externalSystemType, "ex_sys_type"));
        st.exchange(use(poolEntry.data, "data"));
        st.exchange(use(poolEntry.parent, "parent"));
        int isDeleted = poolEntry.isDeleted ? 1 : 0;
        st.exchange(use(isDeleted, "is_del"));

        std::string actIDStrKey;
        if (poolEntry.accountID)
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

        auto timer = insert ? getDatabase().getInsertTimer(
                                  "external_system_account_id_pool")
                            : getDatabase().getUpdateTimer(
                                  "external_system_account_id_pool");
        st.execute(true);

        if (st.get_affected_rows() != 1)
        {
            throw std::runtime_error("could not update SQL");
        }

        if (insert)
        {
            mStorageHelper.getLedgerDelta().addEntry(*poolEntryFrame);
        }
        else
        {
            mStorageHelper.getLedgerDelta().modEntry(*poolEntryFrame);
        }
    }
    catch (std::exception ex)
    {
        CLOG(ERROR, Logging::ENTRY_LOGGER)
            << "Failed to update external system account id pool entry"
            << xdr::xdr_to_string(entry) << " reason: " << ex.what();
        throw_with_nested(std::runtime_error(
            "Failed to update external system account id pool entry"));
    }
}

void
ExternalSystemAccountIDPoolEntryHelperImpl::storeAdd(LedgerEntry const& entry)
{
    return storeUpdateHelper(true, entry);
}

void
ExternalSystemAccountIDPoolEntryHelperImpl::storeChange(
    LedgerEntry const& entry)
{
    return storeUpdateHelper(false, entry);
}

void
ExternalSystemAccountIDPoolEntryHelperImpl::storeDelete(LedgerKey const& key)
{
    Database& db = getDatabase();
    auto timer = db.getDeleteTimer("external_system_account_id_pool");
    auto prep = db.getPreparedStatement(
        "DELETE FROM external_system_account_id_pool WHERE id = :id");
    auto& st = prep.statement();
    const auto& poolEntry = key.externalSystemAccountIDPoolEntry();
    st.exchange(use(poolEntry.poolEntryID, "id"));
    st.define_and_bind();
    st.execute(true);

    mStorageHelper.getLedgerDelta().deleteEntry(key);
}

void
ExternalSystemAccountIDPoolEntryHelperImpl::dropAll()
{
    getDatabase().getSession()
        << "DROP TABLE IF EXISTS external_system_account_id_pool;";
    getDatabase().getSession()
        << "CREATE TABLE external_system_account_id_pool"
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

    fixTypes();
}

void
ExternalSystemAccountIDPoolEntryHelperImpl::fixTypes()
{
    getDatabase().getSession()
        << "ALTER TABLE external_system_account_id_pool ALTER "
           "parent SET DATA TYPE BIGINT;";
    getDatabase().getSession()
        << "ALTER TABLE external_system_account_id_pool ALTER "
           "external_system_type SET DATA TYPE BIGINT;";
}

void
ExternalSystemAccountIDPoolEntryHelperImpl::parentToNumeric()
{
    getDatabase().getSession()
        << "ALTER TABLE external_system_account_id_pool ALTER "
           "parent SET DATA TYPE NUMERIC(20, 0);";
}

bool
ExternalSystemAccountIDPoolEntryHelperImpl::exists(LedgerKey const& key)
{
    auto const& poolEntry = key.externalSystemAccountIDPoolEntry();
    return exists(poolEntry.poolEntryID);
}

bool
ExternalSystemAccountIDPoolEntryHelperImpl::exists(uint64_t poolEntryID)
{
    int exists = 0;
    auto timer =
        getDatabase().getSelectTimer("external_system_account_id_pool_exists");
    auto prep = getDatabase().getPreparedStatement(
        "SELECT EXISTS (SELECT NULL FROM external_system_account_id_pool WHERE "
        "id = :id)");
    auto& st = prep.statement();
    st.exchange(use(poolEntryID, "id"));
    st.exchange(into(exists));
    st.define_and_bind();
    st.execute(true);

    return exists != 0;
}

bool
ExternalSystemAccountIDPoolEntryHelperImpl::existsForAccount(
    int32 externalSystemType, AccountID accountID)
{
    int exists = 0;
    auto timer =
        getDatabase().getSelectTimer("external_system_account_id_pool_exists");
    auto prep = getDatabase().getPreparedStatement(
        "SELECT EXISTS (SELECT NULL FROM external_system_account_id_pool WHERE "
        "external_system_type = :ex_sys_type AND account_id = :aid)");
    auto& st = prep.statement();
    st.exchange(use(externalSystemType, "ex_sys_type"));

    std::string actIDStrKey = PubKeyUtils::toStrKey(accountID);
    st.exchange(use(actIDStrKey, "aid"));

    st.exchange(into(exists));
    st.define_and_bind();
    st.execute(true);

    return exists != 0;
}

LedgerKey
ExternalSystemAccountIDPoolEntryHelperImpl::getLedgerKey(
    LedgerEntry const& from)
{
    LedgerKey ledgerKey;
    ledgerKey.type(from.data.type());
    ledgerKey.externalSystemAccountIDPoolEntry().poolEntryID =
        from.data.externalSystemAccountIDPoolEntry().poolEntryID;
    return ledgerKey;
}

EntryFrame::pointer
ExternalSystemAccountIDPoolEntryHelperImpl::storeLoad(LedgerKey const& key)
{
    auto const& poolEntry = key.externalSystemAccountIDPoolEntry();
    return load(poolEntry.poolEntryID);
}

EntryFrame::pointer
ExternalSystemAccountIDPoolEntryHelperImpl::fromXDR(LedgerEntry const& from)
{
    return std::make_shared<ExternalSystemAccountIDPoolEntryFrame>(from);
}

uint64_t
ExternalSystemAccountIDPoolEntryHelperImpl::countObjects()
{
    uint64_t count = 0;
    getDatabase().getSession()
        << "SELECT COUNT(*) FROM external_system_account_id_pool;",
        into(count);
    return count;
}

ExternalSystemAccountIDPoolEntryFrame::pointer
ExternalSystemAccountIDPoolEntryHelperImpl::load(uint64_t poolEntryID)
{
    std::string sql = select;
    sql += +" WHERE id = :id";

    Database& db = getDatabase();
    auto prep = db.getPreparedStatement(sql);
    auto& st = prep.statement();
    st.exchange(use(poolEntryID, "id"));

    ExternalSystemAccountIDPoolEntryFrame::pointer result;
    auto timer = db.getSelectTimer("external_system_account_id_pool");
    load(prep, [&result](LedgerEntry const& entry) {
        result = std::make_shared<ExternalSystemAccountIDPoolEntryFrame>(entry);
    });

    if (!result)
    {
        return nullptr;
    }

    mStorageHelper.getLedgerDelta().recordEntry(*result);

    return result;
}

ExternalSystemAccountIDPoolEntryFrame::pointer
ExternalSystemAccountIDPoolEntryHelperImpl::load(int32 type,
                                                 std::string const data)
{
    std::string sql = select;
    sql += +" WHERE external_system_type = :ex_sys_type AND data = :data ";
    auto prep = getDatabase().getPreparedStatement(sql);
    auto& st = prep.statement();
    st.exchange(use(type, "ex_sys_type"));
    st.exchange(use(data, "data"));

    ExternalSystemAccountIDPoolEntryFrame::pointer result;
    auto timer =
        getDatabase().getSelectTimer("external_system_account_id_pool");
    load(prep, [&result](LedgerEntry const& entry) {
        result = std::make_shared<ExternalSystemAccountIDPoolEntryFrame>(entry);
    });

    if (!result)
    {
        return nullptr;
    }

    mStorageHelper.getLedgerDelta().recordEntry(*result);

    return result;
}

ExternalSystemAccountIDPoolEntryFrame::pointer
ExternalSystemAccountIDPoolEntryHelperImpl::load(int32 externalSystemType,
                                                 AccountID accountID)
{
    std::string sql = select;
    sql += +" WHERE external_system_type = :ex_sys_type AND account_id = :aid";
    Database& db = getDatabase();
    auto prep = db.getPreparedStatement(sql);
    auto& st = prep.statement();
    st.exchange(use(externalSystemType, "ex_sys_type"));

    std::string actIDStrKey = PubKeyUtils::toStrKey(accountID);
    st.exchange(use(actIDStrKey, "aid"));

    ExternalSystemAccountIDPoolEntryFrame::pointer result;
    auto timer = db.getSelectTimer("external_system_account_id_pool");
    load(prep, [&result](LedgerEntry const& entry) {
        result = std::make_shared<ExternalSystemAccountIDPoolEntryFrame>(entry);
    });

    if (!result)
    {
        return nullptr;
    }

    mStorageHelper.getLedgerDelta().recordEntry(*result);

    return result;
}

void
ExternalSystemAccountIDPoolEntryHelperImpl::load(
    StatementContext& prep, std::function<void(LedgerEntry const&)> processor)
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
        throw_with_nested(std::runtime_error(
            "Failed to load external system account id pool entry"));
    }
}

ExternalSystemAccountIDPoolEntryFrame::pointer
ExternalSystemAccountIDPoolEntryHelperImpl::loadAvailablePoolEntry(
    LedgerManager& ledgerManager, int32 externalSystemType)
{
    std::string sql = select;
    sql += +" WHERE external_system_type = :ex_sys_type AND expires_at < :time "
            "AND is_deleted = FALSE "
            "ORDER BY account_id = '' DESC, expires_at, id LIMIT 1";

    Database& db = getDatabase();
    auto prep = db.getPreparedStatement(sql);
    auto& st = prep.statement();
    st.exchange(use(externalSystemType, "ex_sys_type"));

    uint64_t time = ledgerManager.getCloseTime();
    st.exchange(use(time, "time"));

    ExternalSystemAccountIDPoolEntryFrame::pointer result;
    auto timer = db.getSelectTimer("external system account id pool");
    load(prep, [&result](LedgerEntry const& entry) {
        result = std::make_shared<ExternalSystemAccountIDPoolEntryFrame>(entry);
    });

    if (!result)
    {
        return nullptr;
    }

    return result;
}

std::vector<ExternalSystemAccountIDPoolEntryFrame::pointer>
ExternalSystemAccountIDPoolEntryHelperImpl::loadPool()
{
    std::vector<ExternalSystemAccountIDPoolEntryFrame::pointer> retPool;
    std::string sql = select;
    auto prep = getDatabase().getPreparedStatement(sql);

    auto timer =
        getDatabase().getSelectTimer("external system account id pool");
    load(prep, [&retPool](LedgerEntry const& of) {
        retPool.emplace_back(
            std::make_shared<ExternalSystemAccountIDPoolEntryFrame>(of));
    });
    return retPool;
}

Database&
ExternalSystemAccountIDPoolEntryHelperImpl::getDatabase()
{
    return mStorageHelper.getDatabase();
}
} // namespace stellar
