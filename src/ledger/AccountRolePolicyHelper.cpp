#include "AccountRolePolicyHelper.h"
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
AccountRolePolicyHelper::dropAll(Database& db)
{
    db.getSession() << "DROP TABLE IF EXISTS account_role_policies;";
    db.getSession()
        << "CREATE TABLE account_role_policies"
           "("
           "id             BIGINT                 NOT NULL CHECK (id > 0),"
           "owner_id       VARCHAR(56)            NOT NULL,"
           "role           BIGINT                 NOT NULL CHECK (role >= 0),"
           "resource       TEXT                   NOT NULL,"
           "action         TEXT                   NOT NULL,"
           "effect         SMALLINT               NOT NULL CHECK (effect >= 0 "
           "AND effect <= 1),"
           "last_modified  INT                    NOT NULL,"
           "version        INT                    NOT NULL,"
           "PRIMARY KEY(id, ownerid),"
           "FOREGIN KEY(role) REFERENCES account_roles(id) ON DELETE CASCADE "
           "ON UPDATE CASCADE"
           ");";
}

void
AccountRolePolicyHelper::storeAdd(LedgerEntry const& entry)
{
    storeUpdate(entry, true);
}

void
AccountRolePolicyHelper::storeChange(LedgerEntry const& entry)
{
    storeUpdate(entry, false);
}

void
AccountRolePolicyHelper::storeDelete(LedgerKey const& key)
{
    Database& db = mStorageHelper.getDatabase();

    flushCachedEntry(key);

    auto timer = db.getDeleteTimer("account_role_policy");
    auto prep = db.getPreparedStatement(
        "DELETE FROM account_role_policies WHERE id=:id AND owner_id=:ow");
    auto& st = prep.statement();

    const std::string ownerID =
        PubKeyUtils::toStrKey(key.accountRolePolicy().ownerID);
    st.exchange(use(key.accountRolePolicy().accountRolePolicyID));
    st.exchange(use(ownerID));
    st.define_and_bind();
    st.execute(true);

    mStorageHelper.getLedgerDelta().deleteEntry(key);
}

void
AccountRolePolicyHelper::storeUpdate(LedgerEntry const& entry, bool insert)
{
    const auto accountRolePolicyFrame =
        make_shared<AccountRolePolicyFrame>(entry);

    accountRolePolicyFrame->ensureValid();
    accountRolePolicyFrame->touch(mStorageHelper.getLedgerDelta());

    LedgerKey const& key = accountRolePolicyFrame->getKey();
    flushCachedEntry(key);

    const uint64_t id = accountRolePolicyFrame->getID();
    const uint64_t roleID = accountRolePolicyFrame->getRoleID();
    const std::string resource = accountRolePolicyFrame->getResource();
    const std::string action = accountRolePolicyFrame->getAction();
    const std::string ownerIDStrKey =
        PubKeyUtils::toStrKey(accountRolePolicyFrame->getOwnerID());
    const auto effect =
        static_cast<int32_t>(accountRolePolicyFrame->getEffect());
    const auto version = static_cast<int32_t>(entry.ext.v());

    std::string sql;

    if (insert)
    {
        sql = std::string("INSERT INTO identity_policies (id, owner_id, "
                          "role, resource, action, effect, last_modified, "
                          "version) "
                          "VALUES (:id, :ow, :r, :rs, :ac, :ef, :lm, :v)");
    }
    else
    {
        sql =
            std::string("UPDATE identity_policies "
                        "SET    role=:r, resource=:rs, action=:ac, effect=:ef, "
                        "lastmodified=:lm, version=:v "
                        "WHERE  id=:id AND ownerid=:ow");
    }

    Database& db = mStorageHelper.getDatabase();
    auto prep = db.getPreparedStatement(sql);

    {
        soci::statement& st = prep.statement();
        st.exchange(use(id, "id"));
        st.exchange(use(ownerIDStrKey, "ow"));
        st.exchange(use(roleID, "r"));
        st.exchange(use(resource, "rs"));
        st.exchange(use(action, "ac"));
        st.exchange(use(effect, "ef"));
        st.exchange(use(accountRolePolicyFrame->getLastModified(), "lm"));
        st.exchange(use(version, "v"));

        st.define_and_bind();

        auto timer = insert ? db.getInsertTimer("account_role_policy")
                            : db.getUpdateTimer("account_role_policy");
        st.execute(true);

        if (st.get_affected_rows() != 1)
        {
            throw std::runtime_error("Could not update Ledger");
        }

        if (insert)
        {
            mStorageHelper.getLedgerDelta().addEntry(*accountRolePolicyFrame);
        }
        else
        {
            mStorageHelper.getLedgerDelta().modEntry(*accountRolePolicyFrame);
        }
    }
}

bool
AccountRolePolicyHelper::exists(LedgerKey const& key)
{
    int exists = 0;
    auto timer = mStorageHelper.getDatabase().getSelectTimer(
        "account_role_policy_exists");
    auto prep = mStorageHelper.getDatabase().getPreparedStatement(
        "SELECT EXISTS (SELECT NULL FROM identity_policies "
        "WHERE id=:id AND ownerid =:ow)");
    auto& st = prep.statement();

    const std::string ownerIDStrKey =
        PubKeyUtils::toStrKey(key.accountRolePolicy().ownerID);
    st.exchange(use(key.accountRolePolicy().accountRolePolicyID));
    st.exchange(use(ownerIDStrKey));
    st.exchange(into(exists));

    st.define_and_bind();
    st.execute(true);

    return exists != 0;
}

LedgerKey
AccountRolePolicyHelper::getLedgerKey(LedgerEntry const& from)
{
    if (from.data.type() != LedgerEntryType::ACCOUNT_ROLE_POLICY)
    {
        throw std::runtime_error("Not a role policy entry.");
    }

    LedgerKey ledgerKey;
    ledgerKey.type(LedgerEntryType::ACCOUNT_ROLE_POLICY);
    ledgerKey.accountRole().accountRoleID =
        from.data.accountRole().accountRoleID;
    return ledgerKey;
}

EntryFrame::pointer
AccountRolePolicyHelper::fromXDR(LedgerEntry const& from)
{
    return make_shared<AccountRolePolicyFrame>(from);
}

EntryFrame::pointer
AccountRolePolicyHelper::storeLoad(LedgerKey const& key)
{
    if (key.type() != LedgerEntryType::ACCOUNT_ROLE_POLICY)
    {
        throw std::runtime_error("Not a role policy entry.");
    }
    if (cachedEntryExists(key))
    {
        auto cachedEntry = getCachedEntry(key);
        return cachedEntry
                   ? std::make_shared<AccountRolePolicyFrame>(*cachedEntry)
                   : nullptr;
    }

    uint64_t roleID;
    std::string resource;
    std::string action;
    int32_t effect;
    int32_t version;

    const std::string ownerIDStrKey =
        PubKeyUtils::toStrKey(key.accountRolePolicy().ownerID);

    LedgerEntry le;
    le.data.type(LedgerEntryType::ACCOUNT_ROLE_POLICY);

    std::string name;
    auto prep = mStorageHelper.getDatabase().getPreparedStatement(
        "SELECT role, resource, action, effect, version, lastmodified "
        "FROM identity_policies "
        "WHERE id =:id AND ownerid =:ow");
    auto& st = prep.statement();
    st.exchange(use(key.accountRolePolicy().accountRolePolicyID));
    st.exchange(use(ownerIDStrKey));
    st.exchange(into(roleID));
    st.exchange(into(resource));
    st.exchange(into(action));
    st.exchange(into(effect));
    st.exchange(into(version));
    st.exchange(into(le.lastModifiedLedgerSeq));
    st.define_and_bind();

    auto timer =
        mStorageHelper.getDatabase().getSelectTimer("identity_policies");
    st.execute(true);

    if (!st.got_data())
    {
        putCachedEntry(key, nullptr);
        return nullptr;
    }

    auto result = make_shared<AccountRolePolicyFrame>(le);
    auto& policyEntry = result->getPolicyEntry();

    policyEntry.accountRolePolicyID =
        key.accountRolePolicy().accountRolePolicyID;
    policyEntry.ownerID = key.accountRolePolicy().ownerID;
    policyEntry.accountRoleID = roleID;
    policyEntry.resource = resource;
    policyEntry.action = action;
    policyEntry.effect = static_cast<AccountRolePolicyEffect>(effect);
    policyEntry.ext.v(static_cast<LedgerVersion>(version));

    std::shared_ptr<LedgerEntry const> pEntry =
        std::make_shared<LedgerEntry const>(result->mEntry);

    putCachedEntry(key, pEntry);

    return result;
}
} // namespace stellar