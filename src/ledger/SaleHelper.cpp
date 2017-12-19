//
// Created by kirill on 05.12.17.
//

#include "SaleHelper.h"
#include "xdrpp/printer.h"
#include "LedgerDelta.h"
#include "util/basen.h"

using namespace soci;
using namespace std;

namespace stellar
{
using xdr::operator<;

const char* selectorSale =
    "SELECT id, owner_id, base_asset, quote_asset, name, start_time, "
    "end_time, soft_cap, hard_cap, current_cap, details, version, lastmodified FROM sale";

void SaleHelper::dropAll(Database& db)
{
    db.getSession() << "DROP TABLE IF EXISTS sale;";
    db.getSession() << "CREATE TABLE sale"
        "("
        "id           BIGINT        NOT NULL CHECK (id >= 0),"
        "owner_id     VARCHAR(56)   NOT NULL,"
        "base_asset   VARCHAR(16)   NOT NULL,"
        "quote_asset  VARCHAR(16)   NOT NULL,"
        "name         TEXT          NOT NULL,"
        "start_time   BIGINT        NOT NULL CHECK (start_time >= 0),"
        "end_time     BIGINT        NOT NULL CHECK (end_time >= 0),"
        "soft_cap     NUMERIC(20,0) NOT NULL CHECK (soft_cap >= 0),"
        "hard_cap     NUMERIC(20,0) NOT NULL CHECK (hard_cap >= 0),"
        "current_cap  NUMERIC(20,0) NOT NULL CHECK (current_cap >= 0),"
        "details      TEXT          NOT NULL,"
        "version      INT           NOT NULL,"
        "lastmodified INT           NOT NULL,"
        "PRIMARY KEY (id)"
        ");";
}

void SaleHelper::storeAdd(LedgerDelta& delta, Database& db,
                          LedgerEntry const& entry)
{
    storeUpdateHelper(delta, db, true, entry);
}

void SaleHelper::storeChange(LedgerDelta& delta, Database& db,
                             LedgerEntry const& entry)
{
    storeUpdateHelper(delta, db, false, entry);
}

void SaleHelper::storeDelete(LedgerDelta& delta, Database& db,
                             LedgerKey const& key)
{
    flushCachedEntry(key, db);
    auto timer = db.getDeleteTimer("sale");
    auto prep = db.getPreparedStatement("DELETE FROM sale WHERE id=:id");
    auto& st = prep.statement();
    st.exchange(use(key.sale().saleID));
    st.define_and_bind();
    st.execute(true);
    delta.deleteEntry(key);
}

bool SaleHelper::exists(Database& db, LedgerKey const& key)
{
    if (cachedEntryExists(key, db))
    {
        return true;
    }

    auto timer = db.getSelectTimer("sale_exists");
    auto prep =
        db.
        getPreparedStatement("SELECT EXISTS (SELECT NULL FROM sale WHERE id=:id)");
    auto& st = prep.statement();
    st.exchange(use(key.sale().saleID));
    auto exists = 0;
    st.exchange(into(exists));
    st.define_and_bind();
    st.execute(true);

    return exists != 0;
}

LedgerKey SaleHelper::getLedgerKey(LedgerEntry const& from)
{
    LedgerKey ledgerKey;
    ledgerKey.type(from.data.type());
    ledgerKey.sale().saleID = from.data.sale().saleID;
    return ledgerKey;
}

EntryFrame::pointer SaleHelper::fromXDR(LedgerEntry const& from)
{
    return std::make_shared<SaleFrame>(from);
}

uint64_t SaleHelper::countObjects(soci::session& sess)
{
    uint64_t count = 0;
    sess << "SELECT COUNT(*) FROM sale;", into(count);
    return count;
}

void SaleHelper::storeUpdateHelper(LedgerDelta& delta, Database& db,
                                   const bool insert,
                                   const LedgerEntry& entry)
{
    auto saleFrame = make_shared<SaleFrame>(entry);
    saleFrame->touch(delta);
    const auto saleEntry = saleFrame->getSaleEntry();

    if (!saleFrame->isValid())
    {
        CLOG(ERROR, Logging::ENTRY_LOGGER)
            << "Unexpected state - sale is invalid: "
            << xdr::xdr_to_string(saleEntry);
        throw runtime_error("Unexpected state - sale is invalid");
    }

    const auto key = saleFrame->getKey();
    flushCachedEntry(key, db);
    string sql;

    auto version = static_cast<int32_t>(saleEntry.ext.v());

    if (insert)
    {
        sql =
            "INSERT INTO sale (id, owner_id, base_asset, quote_asset, name, start_time,"
            " end_time, soft_cap, hard_cap, current_cap, details, version, lastmodified)"
            " VALUES (:id, :owner_id, :base_asset, :quote_asset, :name, :start_time,"
            " :end_time, :soft_cap, :hard_cap, :current_cap, :details, :v, :lm";
    }
    else
    {
        sql =
            "UPDATE sale SET owner_id=:owner_id, base_asset = :base_asset, quote_asset = :quote_asset, name = :name, start_time = :start_time,"
            " end_time= :end_time, soft_cap = :soft_cap, hard_cap = :hard_cap, current_cap = :current_cap, details = :details, version=:v, lastmodified=:lm"
            " WHERE id = :id";
    }

    auto prep = db.getPreparedStatement(sql);
    auto& st = prep.statement();

    st.exchange(use(saleEntry.saleID, "id"));
    st.exchange(use(saleEntry.ownerID, "owner_id"));
    st.exchange(use(saleEntry.baseAsset, ":base_asset"));
    st.exchange(use(saleEntry.quoteAsset, "quote_asset"));
    st.exchange(use(saleEntry.name, "name"));
    st.exchange(use(saleEntry.startTime, "start_time"));
    st.exchange(use(saleEntry.endTime, "end_time"));
    st.exchange(use(saleEntry.softCap, "soft_cap"));
    st.exchange(use(saleEntry.hardCap, "hard_cap"));
    st.exchange(use(saleEntry.currentCap, "current_cap"));
    st.exchange(use(saleEntry.details, "details"));
    st.exchange(use(version, "v"));
    st.exchange(use(saleFrame->mEntry.lastModifiedLedgerSeq, "lm"));
    st.define_and_bind();

    auto timer = insert
                     ? db.getInsertTimer("sale")
                     : db.getUpdateTimer("sale");
    st.execute(true);

    if (st.get_affected_rows() != 1)
    {
        CLOG(ERROR, Logging::ENTRY_LOGGER) << "Failed to update sale with id: " << saleEntry.saleID;
        throw runtime_error("Failed to update sale");
    }

    if (insert)
    {
        delta.addEntry(*saleFrame);
    }
    else
    {
        delta.modEntry(*saleFrame);
    }
}

void SaleHelper::loadRequests(StatementContext& prep,
                              const function<void(LedgerEntry const&)>
                              saleProcessor) const
{
    LedgerEntry le;
    le.data.type(LedgerEntryType::SALE);
    auto& oe = le.data.sale();
    int version;

    statement& st = prep.statement();
    st.exchange(into(oe.saleID));
    st.exchange(into(oe.ownerID));
    st.exchange(into(oe.baseAsset));
    st.exchange(into(oe.quoteAsset));
    st.exchange(into(oe.name));
    st.exchange(into(oe.startTime));
    st.exchange(into(oe.endTime));
    st.exchange(into(oe.softCap));
    st.exchange(into(oe.hardCap));
    st.exchange(into(oe.currentCap));
    st.exchange(into(oe.details));
    st.exchange(into(version));
    st.exchange(into(le.lastModifiedLedgerSeq));
    st.define_and_bind();
    st.execute(true);

    while (st.got_data())
    {
        oe.ext.v(static_cast<LedgerVersion>(version));
        if (!SaleFrame::isValid(oe))
        {
            CLOG(ERROR, Logging::ENTRY_LOGGER) <<
                "Unexpected state: invalid sale entry: " << xdr::
                xdr_to_string(oe);
            throw runtime_error("Invalid sale");
        }

        saleProcessor(le);
        st.fetch();
    }
}
SaleFrame::pointer SaleHelper::loadSale(uint64_t saleID, Database& db,
                                           LedgerDelta* delta)
{
    LedgerKey key;
    key.type(LedgerEntryType::SALE);
    key.sale().saleID = saleID;
    if (cachedEntryExists(key, db))
    {
        const auto p = getCachedEntry(key, db);
        return p ? std::make_shared<SaleFrame>(*p) : nullptr;
    }

    string sql = selectorSale;
    sql += +" WHERE id = :id";
    auto prep = db.getPreparedStatement(sql);
    auto& st = prep.statement();
    st.exchange(use(saleID));

    SaleFrame::pointer retSale;
    auto timer = db.getSelectTimer("sale");
    loadRequests(prep, [&retSale](LedgerEntry const& entry)
    {
        retSale = make_shared<SaleFrame>(entry);
    });

    if (!retSale)
    {
        putCachedEntry(key, nullptr, db);
        return nullptr;
    }

    if (delta)
    {
        delta->recordEntry(*retSale);
    }

    const auto pEntry = std::make_shared<LedgerEntry>(retSale->mEntry);
    putCachedEntry(key, pEntry, db);
    return retSale;
}

vector<SaleFrame::pointer> SaleHelper::loadSales(AssetCode const& base,
    AssetCode const& quote, Database& db)
{
    string sql = selectorSale;
    sql += +" WHERE base_asset = :base_asset AND quote_asset = :quote_asset";
    auto prep = db.getPreparedStatement(sql);
    auto& st = prep.statement();
    st.exchange(use(base, "base_asset"));
    st.exchange(use(quote, "quote_asset"));

    vector<SaleFrame::pointer> result;
    auto timer = db.getSelectTimer("sale");
    loadRequests(prep, [&result](LedgerEntry const& entry)
    {
            result.push_back(make_shared<SaleFrame>(entry));
    });

    return result;
}

EntryFrame::pointer SaleHelper::storeLoad(LedgerKey const& key, Database& db)
{
    return loadSale(key.sale().saleID, db);
}
}
