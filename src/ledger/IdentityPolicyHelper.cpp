
#include "IdentityPolicyHelper.h"
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
IdentityPolicyHelper::dropAll(Database& db)
{
    db.getSession() << "DROP TABLE IF EXISTS identity_policies;";
    db.getSession()
        << "CREATE TABLE identity_policies"
           "("
           "id             BIGINT                 NOT NULL CHECK (id > 0),"
           "ownerid        VARCHAR(56)            NOT NULL,"
           "role           BIGINT                 NOT NULL CHECK (role >= 0),"
           "resource       TEXT                   NOT NULL,"
           "action         TEXT                   NOT NULL,"
           "effect         SMALLINT               NOT NULL CHECK (effect >= 0 AND effect <= 1),"
           "lastmodified   INT                    NOT NULL,"
           "version        INT                    NOT NULL,"
           "PRIMARY KEY(id, ownerid)"
           ");";
}

void
IdentityPolicyHelper::storeAdd(LedgerDelta& delta, Database& db,
                               LedgerEntry const& entry)
{
    storeUpdate(delta, db, true, entry);
}

void
IdentityPolicyHelper::storeChange(LedgerDelta& delta, Database& db,
                                  LedgerEntry const& entry)
{
    storeUpdate(delta, db, false, entry);
}

void
IdentityPolicyHelper::storeDelete(LedgerDelta& delta, Database& db,
                                  LedgerKey const& key)
{
    flushCachedEntry(key, db);

    auto timer = db.getDeleteTimer("identity_policy");
    auto prep =
        db.getPreparedStatement("DELETE FROM identity_policies WHERE id=:id AND ownerid=:ow");
    auto& st = prep.statement();

    const std::string ownerIDStrKey = PubKeyUtils::toStrKey(key.identityPolicy().ownerID);

    st.exchange(use(key.identityPolicy().id));
    st.exchange(use(ownerIDStrKey));
    st.define_and_bind();
    st.execute(true);

    delta.deleteEntry(key);
}

void
IdentityPolicyHelper::storeUpdate(LedgerDelta& delta, Database& db, bool insert,
                                  LedgerEntry const& entry)
{
    const auto identityPolicyFrame = make_shared<IdentityPolicyFrame>(entry);

    identityPolicyFrame->ensureValid();
    identityPolicyFrame->touch(delta);

    LedgerKey const& key = identityPolicyFrame->getKey();
    flushCachedEntry(key, db);

    const uint64_t id = identityPolicyFrame->getID();
    const uint64_t priority = identityPolicyFrame->getPriority();
    const std::string resource = identityPolicyFrame->getResource();
    const std::string action = identityPolicyFrame->getAction();
    const std::string ownerIDStrKey = PubKeyUtils::toStrKey(identityPolicyFrame->getOwnerID());
    const auto effect = static_cast<int32_t>(identityPolicyFrame->getEffect());
    const auto version = static_cast<int32_t>(entry.ext.v());

    std::string sql;

    if (insert)
    {
        sql = std::string("INSERT INTO identity_policies (id, priority, "
                          "resource, action, effect, ownerid, version, lastmodified) "
                          "VALUES (:id, :pt, :rs, :ac, :ef, :ow, :v, :lm)");
    }
    else
    {
        sql = std::string("UPDATE identity_policies "
                          "SET    priority=:pt, action=:ac, resource=:rs, effect=:ef, "
                          "version=:v, lastmodified=:lm "
                          "WHERE  id=:id AND ownerid=:ow");
    }

    auto prep = db.getPreparedStatement(sql);

    {
        soci::statement& st = prep.statement();
        st.exchange(use(id, "id"));
        st.exchange(use(priority, "pt"));
        st.exchange(use(resource, "rs"));
        st.exchange(use(action, "ac"));
        st.exchange(use(effect, "ef"));
        st.exchange(use(ownerIDStrKey, "ow"));
        st.exchange(use(version, "v"));
        st.exchange(use(identityPolicyFrame->getLastModified(), "lm"));

        st.define_and_bind();

        auto timer = insert ? db.getInsertTimer("identity_policy")
                            : db.getUpdateTimer("identity_policy");
        st.execute(true);

        if (st.get_affected_rows() != 1)
        {
            throw std::runtime_error("Could not update Ledger");
        }

        if (insert)
        {
            delta.addEntry(*identityPolicyFrame);
        }
        else
        {
            delta.modEntry(*identityPolicyFrame);
        }
    }
}

bool
IdentityPolicyHelper::exists(Database& db, LedgerKey const& key)
{
    int exists = 0;
    auto timer = db.getSelectTimer("identity_policy-exists");
    auto prep = db.getPreparedStatement(
        "SELECT EXISTS (SELECT NULL FROM identity_policies "
        "WHERE id=:id AND ownerid =:ow)");
    auto& st = prep.statement();

    const std::string ownerIDStrKey = PubKeyUtils::toStrKey(key.identityPolicy().ownerID);


    st.exchange(use(key.identityPolicy().id));
    st.exchange(use(ownerIDStrKey));
    st.exchange(into(exists));

    st.define_and_bind();
    st.execute(true);

    return !!exists;
}

bool
IdentityPolicyHelper::exists(Database& db, uint64_t id)
{
    LedgerKey key;

    key.type(LedgerEntryType::IDENTITY_POLICY);
    key.identityPolicy().id = id;

    return exists(db, key);
}

uint64_t
IdentityPolicyHelper::countObjects(soci::session& sess)
{
    uint64_t count = 0;
    sess << "SELECT COUNT(*) FROM identity_policies;", into(count);

    return count;
}

uint64_t
IdentityPolicyHelper::countObjectsForOwner(const AccountID &ownerID, soci::session& sess)
{
    uint64_t count = 0;
    const std::string ownerIDStrKey = PubKeyUtils::toStrKey(ownerID);
    sess << "SELECT COUNT(*) FROM identity_policies WHERE ownerid = :ow", use(ownerIDStrKey), into(count);

    return count;
}

LedgerKey
IdentityPolicyHelper::getLedgerKey(LedgerEntry const& from)
{
    LedgerKey ledgerKey;

    ledgerKey.type(LedgerEntryType::IDENTITY_POLICY);
    ledgerKey.identityPolicy().id = from.data.identityPolicy().id;
    ledgerKey.identityPolicy().ownerID = from.data.identityPolicy().ownerID;

    return ledgerKey;
}

EntryFrame::pointer
IdentityPolicyHelper::fromXDR(LedgerEntry const& from)
{
    return make_shared<IdentityPolicyFrame>(from);
}

EntryFrame::pointer
IdentityPolicyHelper::storeLoad(LedgerKey const& key, Database& db)
{
    return loadIdentityPolicy(key.identityPolicy().id, key.identityPolicy().ownerID, db);
}

IdentityPolicyFrame::pointer
IdentityPolicyHelper::loadIdentityPolicy(uint64_t id, AccountID ownerID, Database& db,
                                         LedgerDelta* delta)
{
    uint64_t priority;
    std::string resource;
    std::string action;
    int32_t effect;
    int32_t version;

    const std::string ownerIDStrKey = PubKeyUtils::toStrKey(ownerID);

    LedgerEntry le;
    le.data.type(LedgerEntryType::IDENTITY_POLICY);

    LedgerKey key;
    key.type(LedgerEntryType::IDENTITY_POLICY);
    key.identityPolicy().id = id;
    key.identityPolicy().ownerID = ownerID;

    if (cachedEntryExists(key, db))
    {
        auto p = getCachedEntry(key, db);
        return p ? std::make_shared<IdentityPolicyFrame>(*p) : nullptr;
    }

    std::string name;
    auto prep = db.getPreparedStatement(
        "SELECT priority, resource, action, effect, version, lastmodified "
        "FROM identity_policies "
        "WHERE id =:id AND ownerid =:ow");
    auto& st = prep.statement();
    st.exchange(use(id));
    st.exchange(use(ownerIDStrKey));
    st.exchange(into(priority));
    st.exchange(into(resource));
    st.exchange(into(action));
    st.exchange(into(effect));
    st.exchange(into(version));
    st.exchange(into(le.lastModifiedLedgerSeq));

    st.define_and_bind();
    {
        auto timer = db.getSelectTimer("identity_policies");
        st.execute(true);
    }

    if (!st.got_data())
    {
        putCachedEntry(key, nullptr, db);
        return nullptr;
    }

    auto result = make_shared<IdentityPolicyFrame>(le);
    auto& policyEntry = result->getIdentityPolicy();

    policyEntry.id = id;
    policyEntry.priority = priority;
    policyEntry.resource = resource;
    policyEntry.action = action;
    policyEntry.effect = static_cast<Effect>(effect);
    policyEntry.ownerID = ownerID;
    policyEntry.ext.v(static_cast<LedgerVersion>(version));

    std::shared_ptr<LedgerEntry const> pEntry =
        std::make_shared<LedgerEntry const>(result->mEntry);

    putCachedEntry(key, pEntry, db);

    return result;
}

} // namespace stellar