
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

    const char* EntityTypeHelper::select = "SELECT id, type, name, lastmodified, version FROM entity_type ";

void
EntityTypeHelper::dropAll(Database& db)
{
    db.getSession() << "DROP TABLE IF EXISTS entity_type;";
    db.getSession() << "CREATE TABLE entity_type"
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

    st.exchange(use(key.entityType().id, "id"));
    st.exchange(use(type, "tp"));
    st.define_and_bind();
    st.execute(true);

    delta.deleteEntry(key);
}

void
EntityTypeHelper::storeUpdate(LedgerDelta& delta, Database& db, bool insert,
                              LedgerEntry const& entry)
{
    const auto entityTypeFrame = make_shared<EntityTypeFrame>(entry);
    auto entityTypeEntry = entityTypeFrame->getEntityType();

    entityTypeFrame->ensureValid();
    entityTypeFrame->touch(delta);

    LedgerKey const& key = entityTypeFrame->getKey();
    flushCachedEntry(key, db);

    std::string sql;

    if (insert)
    {
        sql = std::string("INSERT INTO entity_type (id, type, name, lastmodified, version) "
                          "VALUES (:id, :tp, :nm, :lm, :v)");
    }
    else
    {
        sql = std::string("UPDATE entity_type "
                          "SET    name=:nm, lastmodified=:lm, version=:v "
                          "WHERE  id=:id AND type=:tp");
    }

    auto prep = db.getPreparedStatement(sql);

    {
        soci::statement& st = prep.statement();
        st.exchange(use(entityTypeEntry.id, "id"));

        auto type = static_cast<int32_t>(entityTypeEntry.type);
        st.exchange(use(type, "tp"));
        st.exchange(use(entityTypeEntry.name, "nm"));
        st.exchange(use(entityTypeFrame->mEntry.lastModifiedLedgerSeq, "lm"));

        const auto version = static_cast<int32_t>(entry.ext.v());
        st.exchange(use(version, "v"));

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
        db.getPreparedStatement("SELECT EXISTS (SELECT NULL FROM entity_type "
                                "WHERE id=:id AND type=:tp)");
    auto& st = prep.statement();
    st.exchange(use(key.entityType().id, "id"));
    st.exchange(use(type, "tp"));
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
    sess << "SELECT COUNT(*) FROM entity_type;", into(count);

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
    std::string sql = select;
    sql += "WHERE id = :id AND type = :tp";
    auto prep = db.getPreparedStatement(sql);

    std::string name;
    auto& st = prep.statement();
    st.exchange(use(id, "id"));
    st.exchange(use(typeInt32, "tp"));

    EntityTypeFrame::pointer result;
    auto timer = db.getSelectTimer("entity_type");
    loadEntityType(prep, [&result](LedgerEntry const& entry)
    {
        result = make_shared<EntityTypeFrame>(entry);
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

    void EntityTypeHelper::loadEntityType(StatementContext& prep, const function<void(LedgerEntry const&)> processor)
    {
        LedgerEntry le;
        le.data.type(LedgerEntryType::ENTITY_TYPE);
        auto& oe = le.data.entityType();
        int version;
        int32_t typeInt32;

        statement& st = prep.statement();
        st.exchange(into(oe.id));
        st.exchange(into(typeInt32));
        st.exchange(into(oe.name));
        st.exchange(into(le.lastModifiedLedgerSeq));
        st.exchange(into(version));
        st.define_and_bind();
        st.execute(true);

        while (st.got_data())
        {
            oe.ext.v(static_cast<LedgerVersion>(version));
            oe.type = static_cast<EntityType>(typeInt32);

            EntityTypeFrame::ensureValid(oe);

            processor(le);
            st.fetch();
        }
    }

} // namespace stellar