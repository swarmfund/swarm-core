#include "AccountRolePermissionHelper.h"
#include "ledger/AccountHelper.h"
#include "ledger/AccountTypeLimitsFrame.h"

#include "LedgerDelta.h"
#include "lib/util/format.h"
#include "util/basen.h"
#include "util/types.h"

using namespace soci;
using namespace std;

namespace stellar
{

using xdr::operator<;

void
AccountRolePermissionHelper::dropAll(Database& db)
{
    db.getSession() << "DROP TABLE IF EXISTS account_role_permissions CASCADE;";
    db.getSession()
        << "CREATE TABLE account_role_permissions"
           "("
           "id             BIGINT                 NOT NULL CHECK (id > 0),"
           "role           BIGINT                 NOT NULL CHECK (role >= 0),"
           "operation_type INT                    NOT NULL CHECK (operation_type >= 0),"
           "lastmodified   INT                    NOT NULL,"
           "version        INT                    NOT NULL,"
           "PRIMARY KEY(id), "
           "FOREIGN KEY(role) REFERENCES account_roles(role_id) ON DELETE RESTRICT ON UPDATE CASCADE, "
           "CONSTRAINT unique_p UNIQUE(role, operation_type)"
           ");";
}

AccountRolePermissionHelper::AccountRolePermissionHelper(
    StorageHelper& storageHelper)
    : mDb(storageHelper.getDatabase())
    , mLedgerDelta(storageHelper.getLedgerDelta())
{
}

void
AccountRolePermissionHelper::storeAdd(LedgerEntry const& entry)
{
    storeUpdate(entry, true);
}

void
AccountRolePermissionHelper::storeChange(LedgerEntry const& entry)
{
    storeUpdate(entry, false);
}

void
AccountRolePermissionHelper::storeDelete(LedgerKey const& key)
{
    flushCachedEntry(key);

    auto timer = mDb.getDeleteTimer("account_role_permission");
    auto prep = mDb.getPreparedStatement(
        "DELETE FROM account_role_permissions WHERE id=:id");
    auto& st = prep.statement();

    st.exchange(use(key.accountRolePermission().permissionID));
    st.define_and_bind();
    st.execute(true);

    if (mLedgerDelta)
    {
        mLedgerDelta->deleteEntry(key);
    }
}

void
AccountRolePermissionHelper::storeUpdate(LedgerEntry const& entry, bool insert)
{
    const auto accountRolePolicyFrame =
        make_shared<AccountRolePermissionFrame>(entry);

    accountRolePolicyFrame->ensureValid();
    if (mLedgerDelta)
    {
        accountRolePolicyFrame->touch(*mLedgerDelta);
    }

    LedgerKey const& key = getLedgerKey(entry);
    flushCachedEntry(key);

    const uint64_t id = accountRolePolicyFrame->getID();
    const uint64_t roleID = accountRolePolicyFrame->getRoleID();
    const OperationType opType = accountRolePolicyFrame->getOperationType();
    const auto version = static_cast<int32_t>(entry.ext.v());

    std::string sql;

    if (insert)
    {
        sql = std::string("INSERT INTO account_role_permissions (id, "
                          "role, operation_type, lastmodified, version) "
                          "VALUES (:id, :r, :o, :lm, :v)");
    }
    else
    {
        sql = std::string("UPDATE account_role_permissions "
                          "SET    role=:r, operation_type=:o, "
                          "lastmodified=:lm, version=:v "
                          "WHERE  id=:id");
    }

    auto prep = mDb.getPreparedStatement(sql);

    {
        soci::statement& st = prep.statement();
        st.exchange(use(id, "id"));
        st.exchange(use(roleID, "r"));
        st.exchange(use(static_cast<int32>(opType), "o"));
        st.exchange(
            use(accountRolePolicyFrame->mEntry.lastModifiedLedgerSeq, "lm"));
        st.exchange(use(version, "v"));

        st.define_and_bind();

        auto timer = insert ? mDb.getInsertTimer("account_role_permission")
                            : mDb.getUpdateTimer("account_role_permission");
        st.execute(true);

        if (st.get_affected_rows() != 1)
        {
            throw std::runtime_error("Could not update Ledger");
        }

        if (mLedgerDelta)
        {
            if (insert)
            {
                mLedgerDelta->addEntry(*accountRolePolicyFrame);
            }
            else
            {
                mLedgerDelta->modEntry(*accountRolePolicyFrame);
            }
        }
    }
}

bool
AccountRolePermissionHelper::exists(LedgerKey const& key)
{
    int exists = 0;
    auto timer = mDb.getSelectTimer("account_role_permission_exists");
    auto prep = mDb.getPreparedStatement(
        "SELECT EXISTS (SELECT NULL FROM account_role_permissions "
        "WHERE id=:id)");
    auto& st = prep.statement();

    st.exchange(use(key.accountRolePermission().permissionID));
    st.exchange(into(exists));

    st.define_and_bind();
    st.execute(true);

    return exists != 0;
}

LedgerKey
AccountRolePermissionHelper::getLedgerKey(LedgerEntry const& from)
{
    if (from.data.type() != LedgerEntryType::ACCOUNT_ROLE_PERMISSION)
    {
        throw std::runtime_error("Not a role policy entry.");
    }

    LedgerKey ledgerKey;
    ledgerKey.type(LedgerEntryType::ACCOUNT_ROLE_PERMISSION);
    ledgerKey.accountRolePermission().permissionID =
        from.data.accountRolePermission().permissionID;
    return ledgerKey;
}

EntryFrame::pointer
AccountRolePermissionHelper::fromXDR(LedgerEntry const& from)
{
    return make_shared<AccountRolePermissionFrame>(from);
}

EntryFrame::pointer
AccountRolePermissionHelper::storeLoad(LedgerKey const& key)
{
    if (key.type() != LedgerEntryType::ACCOUNT_ROLE_PERMISSION)
    {
        throw std::runtime_error("Not a role policy entry.");
    }
    if (cachedEntryExists(key))
    {
        auto cachedEntry = getCachedEntry(key);
        return cachedEntry
                   ? std::make_shared<AccountRolePermissionFrame>(*cachedEntry)
                   : nullptr;
    }

    uint64_t roleID;
    int32_t opType;
    int32_t version;

    LedgerEntry le;
    le.data.type(LedgerEntryType::ACCOUNT_ROLE_PERMISSION);

    std::string name;
    auto prep = mDb.getPreparedStatement(
        "SELECT role, operation_type, version, lastmodified "
        "FROM account_role_permissions "
        "WHERE id =:id");
    auto& st = prep.statement();
    st.exchange(use(key.accountRolePermission().permissionID));
    st.exchange(into(roleID));
    st.exchange(into(opType));
    st.exchange(into(version));
    st.exchange(into(le.lastModifiedLedgerSeq));
    st.define_and_bind();

    auto timer = mDb.getSelectTimer("account_role_permissions");
    st.execute(true);

    if (!st.got_data())
    {
        putCachedEntry(key, nullptr);
        return nullptr;
    }

    auto result = make_shared<AccountRolePermissionFrame>(le);
    auto& policyEntry = result->getPermissionEntry();

    policyEntry.permissionID = key.accountRolePermission().permissionID;
    policyEntry.accountRoleID = roleID;
    policyEntry.opType = static_cast<OperationType>(opType);
    policyEntry.ext.v(static_cast<LedgerVersion>(version));

    std::shared_ptr<LedgerEntry const> pEntry =
        std::make_shared<LedgerEntry const>(result->mEntry);

    putCachedEntry(key, pEntry);

    return result;
}

uint64_t
AccountRolePermissionHelper::countObjects()
{
    auto timer = mDb.getSelectTimer("account_role_permission_count");
    auto prep = mDb.getPreparedStatement(
        "SELECT COUNT(*) FROM account_role_permissions");
    auto& st = prep.statement();

    uint64_t count;
    st.exchange(into(count));
    st.define_and_bind();
    st.execute(true);

    return count;
}

Database&
AccountRolePermissionHelper::getDatabase()
{
    return mDb;
}
} // namespace stellar