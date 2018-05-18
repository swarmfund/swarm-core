
#include "EntityTypeHelper.h"
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
EntityTypeHelper::dropAll(Database& db)
{
    db.getSession() << "DROP TABLE IF EXISTS entity_types;";
    db.getSession() << "CREATE TABLE entity_types"
                       "("
                       "id             BIGINT  NOT NULL CHECK (id >= 0),"
                       "type           INT     NOT NULL,"
                       "name           TEXT    NOT NULL,"
                       "lastmodified   INT     NOT NULL,"
                       "version        INT     NOT NULL,"
                       "PRIMARY KEY(id, type)"
                       ");";
}

void
EntityTypeHelper::storeAdd(LedgerDelta& delta, Database& db,
                           LedgerEntry const& entry)
{
    storeUpdate(delta, db, true, entry);
}

void
EntityTypeHelper::storeChange(LedgerDelta& delta, Database& db,
                              LedgerEntry const& entry)
{
    storeUpdate(delta, db, false, entry);
}

void
EntityTypeHelper::storeDelete(LedgerDelta& delta, Database& db,
                              LedgerKey const& key)
{
    flushCachedEntry(key, db);

    const auto type = static_cast<int32_t>(key.entityType().type);
    auto timer = db.getDeleteTimer("entity_type");
    auto prep = db.getPreparedStatement(
        "DELETE FROM entity_type WHERE id=:id AND type=:tp");
    auto& st = prep.statement();

    st.exchange(use(key.entityType().id));
    st.exchange(use(type));
    st.define_and_bind();
    st.execute(true);

    delta.deleteEntry(key);
}

void
EntityTypeHelper::storeUpdate(LedgerDelta& delta, Database& db, bool insert,
                              LedgerEntry const& entry)
{
    const auto entityTypeFrame = make_shared<EntityTypeFrame>(entry);

    entityTypeFrame->ensureValid();
    entityTypeFrame->touch(delta);

    LedgerKey const& key = entityTypeFrame->getKey();
    flushCachedEntry(key, db);

    const int64_t typeID = entityTypeFrame->getEntityTypeID();
    const std::string typeName = entityTypeFrame->getEntityTypeName();
    const auto type = static_cast<int32_t>(entityTypeFrame->getEntityTypeValue());
    const auto version = static_cast<int32_t>(entry.ext.v());

    std::string sql;

    if (insert)
    {
        sql = std::string("INSERT INTO entity_types (id, type, name, version, lastmodified) "
                          "VALUES (:id, :tp, :nm, :v, :lm)");
    }
    else
    {
        sql = std::string("UPDATE entity_types "
                          "SET    name=:nm, version=:v, lastmodified=:lm"
                          "WHERE  id=:id AND type=:tp");
    }

    auto prep = db.getPreparedStatement(sql);

    {
        soci::statement& st = prep.statement();
        st.exchange(use(typeID, "id"));
        st.exchange(use(type, "tp"));
        st.exchange(use(typeName, "nm"));
        st.exchange(use(version, "v"));
        st.exchange(use(entry.lastModifiedLedgerSeq, "lm"));

        st.define_and_bind();

        auto timer = insert ? db.getInsertTimer("entity_type")
                            : db.getUpdateTimer("entity_type");
        st.execute(true);

        if (st.get_affected_rows() != 1)
        {
            throw std::runtime_error("Could not update Ledger");
        }

        if (insert)
        {
            delta.addEntry(*entityTypeFrame);
        }
        else
        {
            delta.modEntry(*entityTypeFrame);
        }
    }
}

bool
EntityTypeHelper::exists(Database& db, LedgerKey const& key)
{
    const auto type = static_cast<int32_t>(key.entityType().type);
    int exists = 0;
    auto timer = db.getSelectTimer("entity-type-exists");
    auto prep =
        db.getPreparedStatement("SELECT EXISTS (SELECT NULL FROM entity_types "
                                "WHERE id=:id AND type=:tp)");
    auto& st = prep.statement();
    st.exchange(use(key.entityType().id));
    st.exchange(use(type));
    st.exchange(into(exists));

    st.define_and_bind();
    st.execute(true);

    return exists != 0;
}

bool
EntityTypeHelper::exists(Database& db, uint64_t id, EntityType type)
{
    LedgerKey key;

    key.type(LedgerEntryType::ENTITY_TYPE);
    key.entityType().id = id;
    key.entityType().type = type;

    return exists(db, key);
}

uint64_t
EntityTypeHelper::countObjects(soci::session& sess)
{
    uint64_t count = 0;
    sess << "SELECT COUNT(*) FROM entity_types;", into(count);

    return count;
}

LedgerKey
EntityTypeHelper::getLedgerKey(LedgerEntry const& from)
{
    LedgerKey ledgerKey;

    ledgerKey.type(LedgerEntryType::ENTITY_TYPE);
    ledgerKey.entityType().id = from.data.entityType().id;
    ledgerKey.entityType().type = from.data.entityType().type;

    return ledgerKey;
}

EntryFrame::pointer
EntityTypeHelper::fromXDR(LedgerEntry const& from)
{
    return make_shared<EntityTypeFrame>(from);
}

EntryFrame::pointer
EntityTypeHelper::storeLoad(LedgerKey const& key, Database& db)
{
    return loadEntityType(key.entityType().id, key.entityType().type, db);
}

EntityTypeFrame::pointer
EntityTypeHelper::loadEntityType(uint64_t id, EntityType type, Database& db,
                                 LedgerDelta* delta)
{
    auto typeInt32 = static_cast<int32_t>(type);
    int32_t version;
    LedgerKey key;
    key.type(LedgerEntryType::ENTITY_TYPE);
    LedgerEntry le;
    le.data.type(LedgerEntryType::ENTITY_TYPE);

    auto& entityTypeKey = key.entityType();
    entityTypeKey.id = id;
    entityTypeKey.type = type;

    if (cachedEntryExists(key, db))
    {
        auto p = getCachedEntry(key, db);
        return p ? std::make_shared<EntityTypeFrame>(*p) : nullptr;
    }

    std::string name;
    auto prep = db.getPreparedStatement("SELECT name, version, lastmodified "
                                        "FROM entity_types "
                                        "WHERE id =:id AND type=:tp");
    auto& st = prep.statement();
    st.exchange(use(id));
    st.exchange(use(typeInt32));
    st.exchange(into(name));
    st.exchange(into(version));
    st.exchange(into(le.lastModifiedLedgerSeq));

    st.define_and_bind();
    {
        auto timer = db.getSelectTimer("entity_types");
        st.execute(true);
    }

    if (!st.got_data())
    {
        putCachedEntry(key, nullptr, db);
        return nullptr;
    }

    auto result = make_shared<EntityTypeFrame>(le);
    auto entityType = result->getEntityType();

    entityType.name = name;
    entityType.type = type;
    entityType.id = id;
    entityType.ext.v(static_cast<LedgerVersion>(version));

    std::shared_ptr<LedgerEntry const> pEntry =
        std::make_shared<LedgerEntry const>(result->mEntry);
    putCachedEntry(key, pEntry, db);

    return result;
}

} // namespace stellar