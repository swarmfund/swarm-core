//
// Created by kirill on 05.12.17.
//

#include "SaleHelper.h"
#include "xdrpp/printer.h"
#include "LedgerDelta.h"
#include "util/basen.h"
#include "SaleQuoteAssetHelper.h"

using namespace soci;
using namespace std;

namespace stellar
{
using xdr::operator<;

const char* selectorSale =
    "SELECT id, owner_id, base_asset, default_quote_asset, start_time, "
    "end_time, soft_cap, hard_cap, details, base_balance, state, version, lastmodified, current_cap_in_base, hard_cap_in_base, sale_type FROM sale";

void SaleHelper::dropAll(Database& db)
{
    db.getSession() << "DROP TABLE IF EXISTS sale;";
    db.getSession() << "CREATE TABLE sale"
        "("
        "id                  BIGINT        NOT NULL CHECK (id >= 0),"
        "owner_id            VARCHAR(56)   NOT NULL,"
        "base_asset          VARCHAR(16)   NOT NULL,"
        "default_quote_asset VARCHAR(16)   NOT NULL,"
        "start_time          BIGINT        NOT NULL CHECK (start_time >= 0),"
        "end_time            BIGINT        NOT NULL CHECK (end_time >= 0),"
        "soft_cap            NUMERIC(20,0) NOT NULL CHECK (soft_cap >= 0),"
        "hard_cap            NUMERIC(20,0) NOT NULL CHECK (hard_cap >= 0),"
        "hard_cap_in_base    NUMERIC(20,0) NOT NULL CHECK (hard_cap_in_base >= 0),"
        "current_cap_in_base NUMERIC(20,0) NOT NULL CHECK (current_cap_in_base >= 0),"
        "details             TEXT          NOT NULL,"
        "base_balance        VARCHAR(56)   NOT NULL,"
        "version             INT           NOT NULL,"
        "lastmodified        INT           NOT NULL,"
        "PRIMARY KEY (id)"
        ");";

    SaleQuoteAssetHelper::dropAll(db);
}

void SaleHelper::addType(Database& db)
{
    db.getSession() << "ALTER TABLE sale ADD COLUMN sale_type INT NOT NULL DEFAULT 0";
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
    SaleQuoteAssetHelper::deleteAllForSale(db, key.sale().saleID);
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
    saleFrame->normalize();
    saleFrame->touch(delta);
    const auto saleEntry = saleFrame->getSaleEntry();
    saleFrame->ensureValid();

    const auto key = saleFrame->getKey();
    flushCachedEntry(key, db);
    string sql;

    auto version = static_cast<int32_t>(saleEntry.ext.v());
    auto state = static_cast<int32_t>(SaleState::ACTIVE);
    if (saleEntry.ext.v() == LedgerVersion::ALLOW_TO_MANAGE_SALE)
    {
        state = static_cast<int32_t>(saleEntry.ext.saleStates().state);
    }

    if (insert)
    {
        sql =
            "INSERT INTO sale (id, owner_id, base_asset, default_quote_asset, start_time,"
            " end_time, soft_cap, hard_cap, details, version, lastmodified, base_balance, state, current_cap_in_base, hard_cap_in_base, sale_type)"
            " VALUES (:id, :owner_id, :base_asset, :default_quote_asset, :start_time,"
            " :end_time, :soft_cap, :hard_cap, :details, :v, :lm, :base_balance, :state, :current_cap_in_base, :hard_cap_in_base, :sale_type)";
    }
    else
    {
        sql =
            "UPDATE sale SET owner_id=:owner_id, base_asset = :base_asset, default_quote_asset = :default_quote_asset, start_time = :start_time,"
            " end_time= :end_time, soft_cap = :soft_cap, hard_cap = :hard_cap, details = :details, version=:v, lastmodified=:lm, "
            " base_balance = :base_balance, state = :state, current_cap_in_base = :current_cap_in_base, hard_cap_in_base = :hard_cap_in_base, sale_type = :sale_type "
            " WHERE id = :id";
    }

    auto prep = db.getPreparedStatement(sql);
    auto& st = prep.statement();

    st.exchange(use(saleEntry.saleID, "id"));
    auto ownerID = PubKeyUtils::toStrKey(saleEntry.ownerID);
    st.exchange(use(ownerID, "owner_id"));
    st.exchange(use(saleEntry.baseAsset, "base_asset"));
    st.exchange(use(saleEntry.defaultQuoteAsset, "default_quote_asset"));
    st.exchange(use(saleEntry.startTime, "start_time"));
    st.exchange(use(saleEntry.endTime, "end_time"));
    st.exchange(use(saleEntry.softCap, "soft_cap"));
    st.exchange(use(saleEntry.hardCap, "hard_cap"));
    st.exchange(use(saleEntry.details, "details"));
    st.exchange(use(version, "v"));
    st.exchange(use(saleFrame->mEntry.lastModifiedLedgerSeq, "lm"));
    auto baseBalance = BalanceKeyUtils::toStrKey(saleEntry.baseBalance);
    st.exchange(use(baseBalance, "base_balance"));
    st.exchange(use(state, "state"));
    st.exchange(use(saleEntry.currentCapInBase, "current_cap_in_base"));
    st.exchange(use(saleEntry.maxAmountToBeSold, "hard_cap_in_base"));
    auto saleType = static_cast<int>(SaleFrame::getSaleType(saleEntry));
    st.exchange(use(saleType, "sale_type"));
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

    SaleQuoteAssetHelper::storeUpdate(db, saleEntry.saleID, saleEntry.quoteAssets, insert);

    if (insert)
    {
        delta.addEntry(*saleFrame);
    }
    else
    {
        delta.modEntry(*saleFrame);
    }
}

void SaleHelper::loadSales(Database& db, StatementContext& prep,
                              const function<void(LedgerEntry const&)>
                              saleProcessor) const
{
   // try
    //{
        LedgerEntry le;
        le.data.type(LedgerEntryType::SALE);
        auto& oe = le.data.sale();
        int version;
    int32_t state;

    statement& st = prep.statement();
    st.exchange(into(oe.saleID));
    st.exchange(into(oe.ownerID));
    st.exchange(into(oe.baseAsset));
    st.exchange(into(oe.defaultQuoteAsset));
    st.exchange(into(oe.startTime));
    st.exchange(into(oe.endTime));
    st.exchange(into(oe.softCap));
    st.exchange(into(oe.hardCap));
    st.exchange(into(oe.details));
    st.exchange(into(oe.baseBalance));
    st.exchange(into(state));
    st.exchange(into(version));
    st.exchange(into(le.lastModifiedLedgerSeq));
    st.exchange(into(oe.currentCapInBase));
    st.exchange(into(oe.maxAmountToBeSold));
    int rawSaleType = 0;
    st.exchange(into(rawSaleType));
    st.define_and_bind();
    st.execute(true);

        while (st.got_data())
        {
            oe.ext.v(static_cast<LedgerVersion>(version));
            if (oe.ext.v() == LedgerVersion::ALLOW_TO_MANAGE_SALE)
            {
                oe.ext.saleStates().state = static_cast<SaleState>(state);
            }
            const auto saleType = SaleType(rawSaleType);
            SaleFrame::setSaleType(oe, saleType);
            oe.quoteAssets = SaleQuoteAssetHelper::loadQuoteAssets(db, oe.saleID);
            SaleFrame::ensureValid(oe);

            saleProcessor(le);
            st.fetch();
        }
    //} catch (exception e)
   // {
     //   throw_with_nested(runtime_error(e.what()));
   // }
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
        auto result = p ? std::make_shared<SaleFrame>(*p) : nullptr;
        if (!!delta && !!result)
        {
            delta->recordEntry(*result);
        }
        return result;
    }

    string sql = selectorSale;
    sql += +" WHERE id = :id";
    auto prep = db.getPreparedStatement(sql);
    auto& st = prep.statement();
    st.exchange(use(saleID));

    SaleFrame::pointer retSale;
    auto timer = db.getSelectTimer("sale");
    loadSales(db, prep, [&retSale](LedgerEntry const& entry)
    {
        retSale = make_shared<SaleFrame>(entry);
        retSale->normalize();
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

SaleFrame::pointer SaleHelper::loadSale(const uint64_t saleID, AssetCode const& base,
    AssetCode const& quote, Database& db, LedgerDelta* delta)
{
    auto sale = loadSale(saleID, db, delta);
    if (!sale)
    {
        return nullptr;
    }

    if (!(sale->getBaseAsset() == base))
    {
        return nullptr;
    }

    for (auto const& quoteAsset : sale->getSaleEntry().quoteAssets)
    {
        if (quoteAsset.quoteAsset == quote)
            return sale;
    }

    return nullptr;
}

std::vector<SaleFrame::pointer> SaleHelper::loadSalesForOwner(AccountID owner,
    Database& db)
{
    string sql = selectorSale;
    sql += +" WHERE owner_id = :owner_id";
    auto prep = db.getPreparedStatement(sql);
    auto& st = prep.statement();
    std::string rawOwnerID = PubKeyUtils::toStrKey(owner);
    st.exchange(use(rawOwnerID));

    vector<SaleFrame::pointer> result;
    auto timer = db.getSelectTimer("sale");
    loadSales(db, prep, [&result](LedgerEntry const& entry)
    {
        SaleFrame::pointer retSale;
        retSale = make_shared<SaleFrame>(entry);
        retSale->normalize();
        result.push_back(retSale);
    });

    return result;
}

EntryFrame::pointer SaleHelper::storeLoad(LedgerKey const& key, Database& db)
{
    return loadSale(key.sale().saleID, db);
}

void SaleHelper::addSaleState(Database &db)
{
    db.getSession() << "ALTER TABLE sale "
                       "ADD state INT NOT NULL DEFAULT 1;";

}
}
