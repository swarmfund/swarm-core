#include "ledger/KeyValueHelperImpl.h"
#include "ledger/LedgerDelta.h"
#include "ledger/StorageHelper.h"
#include <memory>
#include <xdrpp/marshal.h>
#include "util/basen.h"

using namespace soci;

namespace stellar
{

using xdr::operator<;

static const char* selectorKeyValue =
    "SELECT key, value, version, lastmodified FROM key_value_entry";

KeyValueHelperImpl::KeyValueHelperImpl(StorageHelper& storageHelper)
    : mStorageHelper(storageHelper)
{
}

void
KeyValueHelperImpl::dropAll()
{
    Database& db = mStorageHelper.getDatabase();
    db.getSession() << "DROP TABLE IF EXISTS key_value_entry;";
    db.getSession() << "CREATE TABLE key_value_entry"
                       "("
                       "key           TEXT          NOT NULL,"
                       "value         TEXT          NOT NULL,"
                       "version       INT           NOT NULL,"
                       "lastmodified  INT           NOT NULL,"
                       "PRIMARY KEY (key)"
                       ");";
}

void
KeyValueHelperImpl::storeAdd(LedgerEntry const& entry)
{
    storeUpdateHelper(mStorageHelper.getLedgerDelta(),
                      mStorageHelper.getDatabase(), true, entry);
}

void
KeyValueHelperImpl::storeChange(LedgerEntry const& entry)
{
    storeUpdateHelper(mStorageHelper.getLedgerDelta(),
                      mStorageHelper.getDatabase(), false, entry);
}

void
KeyValueHelperImpl::storeDelete(LedgerKey const& key)
{
    flushCachedEntry(key);

    Database& db = mStorageHelper.getDatabase();
    auto timer = db.getDeleteTimer("key_value_entry");
    auto prep =
        db.getPreparedStatement("DELETE FROM key_value_entry WHERE key=:key");
    auto& st = prep.statement();
    auto keyStr = key.keyValue().key;
    st.exchange(use(keyStr));
    st.define_and_bind();
    st.execute(true);
    if (mStorageHelper.getLedgerDelta())
    {
        mStorageHelper.getLedgerDelta()->deleteEntry(key);
    }
}

bool
KeyValueHelperImpl::exists(LedgerKey const& key)
{
    if (cachedEntryExists(key))
    {
        return true;
    }

    Database& db = mStorageHelper.getDatabase();
    auto timer = db.getSelectTimer("key_value_entry_exists");
    auto prep = db.getPreparedStatement(
        "SELECT EXISTS (SELECT NULL FROM key_value_entry WHERE key=:key)");
    auto& st = prep.statement();
    auto keyStr = key.keyValue().key;
    st.exchange(use(keyStr));
    int exists = 0;
    st.exchange(into(exists));
    st.define_and_bind();
    st.execute(true);

    return exists != 0;
}

void
KeyValueHelperImpl::storeUpdateHelper(LedgerDelta* delta, Database& db,
                                      bool insert, LedgerEntry const& entry)
{
    auto keyValueFrame = std::make_shared<KeyValueEntryFrame>(entry);
    auto keyValueEntry = keyValueFrame->getKeyValue();

    if (delta)
    {
        keyValueFrame->touch(*delta);
    }

    auto key = keyValueFrame->getKey();
    flushCachedEntry(key);
    std::string sql;

    auto valueBytes = xdr::xdr_to_opaque(keyValueEntry.value);
    std::string strValue = bn::encode_b64(valueBytes);
    auto version = static_cast<int32_t>(keyValueEntry.ext.v());

    if (insert)
    {
        sql = "INSERT INTO key_value_entry (key, value, version, lastmodified)"
              " VALUES (:key, :value, :v, :lm)";
    }
    else
    {
        sql = "UPDATE key_value_entry SET value=:value, version=:v, "
              "lastmodified=:lm"
              " WHERE key = :key";
    }

    auto prep = db.getPreparedStatement(sql);
    auto& st = prep.statement();

    st.exchange(use(keyValueEntry.key, "key"));
    st.exchange(use(strValue, "value"));
    st.exchange(use(version, "v"));
    st.exchange(use(keyValueFrame->mEntry.lastModifiedLedgerSeq, "lm"));
    st.define_and_bind();

    auto timer = insert ? db.getInsertTimer("key_value_entry")
                        : db.getUpdateTimer("key_value_entry");
    st.execute(true);

    if (st.get_affected_rows() != 1)
    {
        throw std::runtime_error("could not update SQL");
    }

    if (delta)
    {
        if (insert)
        {
            delta->addEntry(*keyValueFrame);
        }
        else
        {
            delta->modEntry(*keyValueFrame);
        }
    }
}

LedgerKey
KeyValueHelperImpl::getLedgerKey(LedgerEntry const& from)
{
    LedgerKey ledgerKey;
    ledgerKey.type(from.data.type());
    ledgerKey.keyValue().key = from.data.keyValue().key;
    return ledgerKey;
}

EntryFrame::pointer
KeyValueHelperImpl::storeLoad(LedgerKey const& key)
{
    return loadKeyValue(key.keyValue().key);
}

EntryFrame::pointer
KeyValueHelperImpl::fromXDR(LedgerEntry const& from)
{
    return std::make_shared<KeyValueEntryFrame>(from);
}

uint64_t
KeyValueHelperImpl::countObjects()
{
    uint64_t count = 0;
    getDatabase().getSession() << "SELECT COUNT(*) FROM key_value_entry;", into(count);
    return count;
}

KeyValueEntryFrame::pointer
KeyValueHelperImpl::loadKeyValue(string256 valueKey)
{
    LedgerKey key;
    key.type(LedgerEntryType::KEY_VALUE);
    key.keyValue().key = valueKey;
    if (cachedEntryExists(key))
    {
        auto p = getCachedEntry(key);
        return p ? std::make_shared<KeyValueEntryFrame>(*p) : nullptr;
    }

    std::string sql = selectorKeyValue;
    sql += " WHERE key = :key";
    Database& db = getDatabase();
    auto prep = db.getPreparedStatement(sql);
    auto& st = prep.statement();
    st.exchange(use(valueKey, "key"));

    KeyValueEntryFrame::pointer retKeyValue;
    auto timer = db.getSelectTimer("key_value_entry");
    loadKeyValues(prep, [&retKeyValue](LedgerEntry const& entry) {
        retKeyValue = std::make_shared<KeyValueEntryFrame>(entry);
    });

    if (!retKeyValue)
    {
        putCachedEntry(key, nullptr);
        return nullptr;
    }

    if (mStorageHelper.getLedgerDelta())
    {
        mStorageHelper.getLedgerDelta()->recordEntry(*retKeyValue);
    }

    auto pEntry = std::make_shared<LedgerEntry>(retKeyValue->mEntry);
    putCachedEntry(key, pEntry);
    return retKeyValue;
}

void
KeyValueHelperImpl::loadKeyValues(
    StatementContext& prep,
    std::function<void(LedgerEntry const&)> keyValueProcessor)
{
    LedgerEntry le;
    le.data.type(LedgerEntryType::KEY_VALUE);
    KeyValueEntry& oe = le.data.keyValue();
    std::string value;
    string256 key;
    int version;

    statement& st = prep.statement();
    st.exchange(into(oe.key));
    st.exchange(into(value));
    st.exchange(into(version));
    st.exchange(into(le.lastModifiedLedgerSeq));
    st.define_and_bind();
    st.execute(true);

    while (st.got_data())
    {

        // unmarshal value
        std::vector<uint8_t> decoded;
        bn::decode_b64(value, decoded);
        xdr::xdr_get unmarshaler(&decoded.front(), &decoded.back() + 1);
        xdr::xdr_argpack_archive(unmarshaler, oe.value);
        unmarshaler.done();

        oe.ext.v(static_cast<LedgerVersion>(version));

        keyValueProcessor(le);
        st.fetch();
    }
}

Database&
KeyValueHelperImpl::getDatabase()
{
    return mStorageHelper.getDatabase();
}
} // namespace stellar
