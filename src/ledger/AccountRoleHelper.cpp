#include "AccountRoleHelper.h"
#include "LedgerDelta.h"

using namespace soci;
using namespace std;

namespace stellar
{
using xdr::operator<;

static const char* accountRolesColumnSelector =
    "SELECT role_id, role_name, lastmodified, version "
    "FROM account_roles";

void
AccountRoleHelper::dropAll()
{
    Database& db = mStorageHelper.getDatabase();

    db.getSession() << "DROP TABLE IF EXISTS account_roles CASCADE;";
    db.getSession()
        << "CREATE TABLE account_roles "
           "("
           "role_id                 BIGINT      NOT NULL CHECK (role_id >= 0), "
           "role_name               TEXT        NOT NULL, "
           "lastmodified            INT         NOT NULL, "
           "version                 INT         NOT NULL DEFAULT 0, "
           "PRIMARY KEY (role_id)"
           ");";
}

AccountRoleHelper::AccountRoleHelper(StorageHelper& storageHelper)
    : mStorageHelper(storageHelper)
{
}

void
AccountRoleHelper::storeUpdate(LedgerEntry const& entry, bool insert)
{
    auto accountRoleFrame = make_shared<AccountRoleFrame>(entry);
    auto accountRoleEntry = accountRoleFrame->getAccountRole();

    if (mStorageHelper.getLedgerDelta())
    {
        accountRoleFrame->touch(*mStorageHelper.getLedgerDelta());
    }
    accountRoleFrame->ensureValid();

    string sql;
    if (insert)
    {
        sql = "INSERT INTO account_roles (role_id, role_name, "
              "lastmodified, version) VALUES (:id, :rn,"
              ":lm, :v)";
    }
    else
    {
        sql = "UPDATE account_roles "
              "SET role_id = :id, role_name = :rn,"
              "lastmodified = :lm, version = :v "
              "WHERE role_id = :id";
    }

    auto prep = mStorageHelper.getDatabase().getPreparedStatement(sql);
    auto& st = prep.statement();

    uint64_t accountRoleID = accountRoleFrame->getID();
    auto version = static_cast<int32_t>(accountRoleEntry.ext.v());

    st.exchange(use(accountRoleID, "id"));
    st.exchange(use(accountRoleFrame->mEntry.lastModifiedLedgerSeq, "lm"));
    st.exchange(use(version, "v"));
    st.exchange(use(accountRoleEntry.accountRoleName, "rn"));
    st.define_and_bind();

    auto timer =
        insert ? mStorageHelper.getDatabase().getInsertTimer("account-role")
               : mStorageHelper.getDatabase().getUpdateTimer("account-role");
    st.execute(true);

    if (st.get_affected_rows() != 1)
        throw runtime_error("could not update SQL");

    if (mStorageHelper.getLedgerDelta())
    {
        if (insert)
            mStorageHelper.getLedgerDelta()->addEntry(*accountRoleFrame);
        else
            mStorageHelper.getLedgerDelta()->modEntry(*accountRoleFrame);
    }
}

void
AccountRoleHelper::storeAdd(LedgerEntry const& entry)
{
    storeUpdate(entry, true);
}

void
AccountRoleHelper::storeChange(LedgerEntry const& entry)
{
    storeUpdate(entry, false);
}

void
AccountRoleHelper::storeDelete(LedgerKey const& key)
{
    Database& db = mStorageHelper.getDatabase();

    auto timer = db.getDeleteTimer("account-role");
    auto prep = db.getPreparedStatement(
        "DELETE FROM account_roles WHERE role_id = :id");
    auto& st = prep.statement();
    auto accountRoleID = key.accountRole().accountRoleID;
    st.exchange(use(accountRoleID, "id"));
    st.define_and_bind();
    st.execute(true);

    if (mStorageHelper.getLedgerDelta())
    {
        mStorageHelper.getLedgerDelta()->deleteEntry(key);
    }
}

bool
AccountRoleHelper::exists(LedgerKey const& key)
{
    if (cachedEntryExists(key))
    {
        return true;
    }

    Database& db = mStorageHelper.getDatabase();

    int exists = 0;
    auto timer = db.getSelectTimer("account-role-exists");
    auto prep = db.getPreparedStatement(
        "SELECT EXISTS (SELECT NULL FROM account_roles WHERE "
        "role_id = :id)");
    auto& st = prep.statement();
    uint64 accountRoleID = key.accountRole().accountRoleID;
    st.exchange(use(accountRoleID, "id"));
    st.exchange(into(exists));
    st.define_and_bind();
    st.execute(true);

    return exists != 0;
}

LedgerKey
stellar::AccountRoleHelper::getLedgerKey(LedgerEntry const& from)
{
    if (from.data.type() != LedgerEntryType::ACCOUNT_ROLE)
    {
        throw std::runtime_error("Not an account role entry.");
    }

    LedgerKey ledgerKey;
    ledgerKey.type(LedgerEntryType::ACCOUNT_ROLE);
    ledgerKey.accountRole().accountRoleID =
        from.data.accountRole().accountRoleID;
    return ledgerKey;
}

EntryFrame::pointer
AccountRoleHelper::storeLoad(LedgerKey const& key)
{
    auto const& accountRoleID = key.accountRole().accountRoleID;

    string sql = accountRolesColumnSelector;
    sql += " WHERE role_id = :id";
    auto prep = mStorageHelper.getDatabase().getPreparedStatement(sql);
    auto& st = prep.statement();

    LedgerEntry le;
    le.data.type(LedgerEntryType::ACCOUNT_ROLE);

    int32_t accountRoleVersion = 0;
    st.exchange(use(accountRoleID, "id"));
    st.exchange(into(le.data.accountRole().accountRoleID));
    st.exchange(into(le.data.accountRole().accountRoleName));
    st.exchange(into(le.lastModifiedLedgerSeq));
    st.exchange(into(accountRoleVersion));
    st.define_and_bind();

    {
        auto timer = mStorageHelper.getDatabase().getSelectTimer("account-role");
        st.execute(true);
    }

    if (!st.got_data())
    {
        putCachedEntry(key, nullptr);
        return EntryFrame::pointer();
    }

    le.data.accountRole().ext.v(LedgerVersion::EMPTY_VERSION);
    AccountRoleFrame::ensureValid(le);

    return make_shared<AccountRoleFrame>(le);
}

Database&
AccountRoleHelper::getDatabase()
{
    return mStorageHelper.getDatabase();
}

EntryFrame::pointer
AccountRoleHelper::fromXDR(LedgerEntry const& from)
{
    return make_shared<AccountRoleFrame>(from);
}

uint64_t
AccountRoleHelper::countObjects()
{
    uint64_t count = 0;
    getDatabase().getSession() << "SELECT COUNT(*) FROM account_roles;",
        into(count);

    return count;
}
} // namespace stellar