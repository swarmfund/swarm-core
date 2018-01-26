#include "SaleQuoteAssetHelper.h"
#include "database/Database.h"

using namespace soci;
using namespace std;

namespace stellar
{
using xdr::operator<;


void SaleQuoteAssetHelper::dropAll(Database& db)
{
    db.getSession() << "DROP TABLE IF EXISTS sale_quote_asset;";
    db.getSession() << "CREATE TABLE sale_quote_asset"
        "("
        "sale_id            BIGINT        NOT NULL CHECK (sale_id >= 0),"
        "quote_asset            VARCHAR(16)   NOT NULL,"
        "price         NUMERIC(20,0) NOT NULL CHECK (price > 0),"
        "quote_balance VARCHAR(56)   NOT NULL,"
        "current_cap      NUMERIC(20,0) NOT NULL CHECK (current_cap >= 0),"
        "version       INT           NOT NULL,"
        "PRIMARY KEY (sale_id, quote_asset)"
        ");";
}

void SaleQuoteAssetHelper::deleteAllForSale(Database& db, uint64_t saleID)
{
    auto prep = db.getPreparedStatement("DELETE FROM sale_quote_asset WHERE sale_id=:id");
    auto& st = prep.statement();
    st.exchange(use(saleID));
    st.define_and_bind();
    st.execute(true);
}

void SaleQuoteAssetHelper::storeUpdate(Database& db, uint64_t const saleID,
    xdr::xvector<SaleQuoteAsset, 100> quoteAssets, const bool insert)
{
    for (auto const& quoteAsset : quoteAssets)
    {
        storeUpdate(db, saleID, quoteAsset, insert);
    }
}

void SaleQuoteAssetHelper::storeUpdate(Database& db, uint64_t const saleID,
    SaleQuoteAsset const& quoteAsset, const bool insert)
{
    string sql;

    auto version = static_cast<int32_t>(quoteAsset.ext.v());

    if (insert)
    {
        sql =
            "INSERT INTO sale_quote_asset (sale_id, quote_asset, price, quote_balance, current_cap, version)"
            " VALUES (:sale_id, :quote_asset, :price, :quote_balance, :current_cap,"
            " :version)";
    }
    else
    {
        sql =
            "UPDATE sale_quote_asset SET price = :price, quote_balance = :quote_balance, current_cap = :current_cap, "
            " version = :version"
            " WHERE sale_id = :sale_id AND quote_asset = :quote_asset";
    }

    auto prep = db.getPreparedStatement(sql);
    auto& st = prep.statement();

    st.exchange(use(saleID, "sale_id"));
    st.exchange(use(quoteAsset.quoteAsset, "quote_asset"));
    st.exchange(use(quoteAsset.price, "price"));
    string balance = BalanceKeyUtils::toStrKey(quoteAsset.quoteBalance);
    st.exchange(use(balance, "quote_balance"));
    st.exchange(use(quoteAsset.currentCap, "current_cap"));
    st.exchange(use(version, "version"));
    st.define_and_bind();
    st.execute(true);

    if (st.get_affected_rows() != 1)
    {
        CLOG(ERROR, Logging::ENTRY_LOGGER) << "Failed to update sale quote asset with id: " << saleID << " " << quoteAsset.quoteAsset;
        throw runtime_error("Failed to update sale quote asset");
    }
}

void SaleQuoteAssetHelper::loadSaleQuoteAsset(StatementContext& prep,
    const std::function<void(SaleQuoteAsset const&)> saleProcessor)
{

    SaleQuoteAsset quoteAsset;
    int version;

    statement& st = prep.statement();
    st.exchange(into(quoteAsset.quoteAsset));
    st.exchange(into(quoteAsset.price));
    st.exchange(into(quoteAsset.quoteBalance));
    st.exchange(into(quoteAsset.currentCap));
    st.exchange(into(version));
    st.define_and_bind();
    st.execute(true);

    while (st.got_data())
    {
        quoteAsset.ext.v(static_cast<LedgerVersion>(version));

        saleProcessor(quoteAsset);
        st.fetch();
    }
}

xdr::xvector<SaleQuoteAsset, 100> SaleQuoteAssetHelper::loadQuoteAssets(Database& db, uint64_t saleID)
{
    const string sql = "SELECT quote_asset, price, quote_balance, current_cap, version FROM sale_quote_asset WHERE sale_id = :sale_id";
    auto prep = db.getPreparedStatement(sql);
    auto& st = prep.statement();
    st.exchange(use(saleID, "sale_id"));

    xdr::xvector<SaleQuoteAsset, 100> result;
    auto timer = db.getSelectTimer("sale");
    loadSaleQuoteAsset(prep, [&result](SaleQuoteAsset const& entry)
    {
        result.push_back(entry);
    });

    return result;
}

}
