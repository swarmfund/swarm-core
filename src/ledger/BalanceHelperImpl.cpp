#include "BalanceHelperImpl.h"
#include "ledger/LedgerDelta.h"
#include "ledger/StorageHelper.h"
#include <memory>
#include <xdrpp/marshal.h>
#include "util/basen.h"

using namespace soci;
using namespace std;

namespace stellar
{

using xdr::operator<;

BalanceHelperImpl::BalanceHelperImpl(StorageHelper& storageHelper)
        : mStorageHelper(storageHelper)
{
    mBalanceColumnSelector = "SELECT balance_id, asset, amount, locked, "
                             "account_id, lastmodified, version "
                             "FROM balance";
}

void
BalanceHelperImpl::dropAll()
{
    Database& db = getDatabase();

    db.getSession() << "DROP TABLE IF EXISTS balance;";
    db.getSession() << "CREATE TABLE balance"
           "("
           "balance_id               VARCHAR(56) NOT NULL,"
           "account_id               VARCHAR(56) NOT NULL,"
           "asset                    VARCHAR(16) NOT NULL,"
           "amount                   BIGINT      NOT NULL CHECK (amount >= 0),"
           "locked                   BIGINT      NOT NULL CHECK (locked >= 0),"
           "lastmodified             INT         NOT NULL, "
           "version                  INT         NOT NULL DEFAULT 0,"
           "PRIMARY KEY (balance_id)"
           ");";
}

void
BalanceHelperImpl::storeAdd(LedgerEntry const& entry)
{
    storeUpdateHelper(true, entry);
}

void
BalanceHelperImpl::storeChange(LedgerEntry const& entry)
{
    storeUpdateHelper(false, entry);
}

void
BalanceHelperImpl::storeDelete(LedgerKey const& key)
{
    flushCachedEntry(key);

    Database& db = getDatabase();
    auto timer = db.getDeleteTimer("delete-balance");
    auto prep =
            db.getPreparedStatement("DELETE FROM balance WHERE balance_id=:id");
    auto& st = prep.statement();
    auto balanceIDStr = BalanceKeyUtils::toStrKey(key.balance().balanceID);
    st.exchange(use(balanceIDStr, "id"));
    st.define_and_bind();
    st.execute(true);
    mStorageHelper.getLedgerDelta().deleteEntry(key);
}

bool
BalanceHelperImpl::exists(LedgerKey const& key)
{
    if (cachedEntryExists(key))
    {
        return true;
    }
    int exists = 0;

    Database& db = getDatabase();
    auto timer = db.getSelectTimer("balance-exists");
    auto prep = db.getPreparedStatement("SELECT EXISTS "
                        "(SELECT NULL FROM balance WHERE balance_id=:id)");
    auto& st = prep.statement();
    auto balanceIDStr = BalanceKeyUtils::toStrKey(key.balance().balanceID);
    st.exchange(use(balanceIDStr, "id"));
    st.exchange(into(exists));
    st.define_and_bind();
    st.execute(true);

    return exists != 0;
}

void
BalanceHelperImpl::storeUpdateHelper(bool insert, LedgerEntry const& entry)
{
    Database& db = getDatabase();
    LedgerDelta& delta = mStorageHelper.getLedgerDelta();

    auto balanceFrame = make_shared<BalanceFrame>(entry);
    auto balanceEntry = balanceFrame->getBalance();

    balanceFrame->touch(delta);
    putCachedEntry(getLedgerKey(entry), make_shared<LedgerEntry>(entry));

    bool isValid = balanceFrame->isValid();
    if (!isValid)
    {
        throw std::runtime_error("Invalid balance");
    }

    std::string accountID = PubKeyUtils::toStrKey(balanceFrame->getAccountID());
    std::string balanceID =
            BalanceKeyUtils::toStrKey(balanceFrame->getBalanceID());
    std::string asset = balanceFrame->getAsset();
    auto version = static_cast<int32_t>(balanceEntry.ext.v());

    string sql;

    if (insert)
    {
        sql = "INSERT INTO balance (balance_id, asset, amount, locked, "
              "                     account_id, lastmodified, version) "
              "VALUES (:id, :as, :am, :ld, :aid, :lm, :v)";
    }
    else
    {
        sql = "UPDATE balance "
              "SET    asset = :as, amount=:am, locked=:ld, account_id=:aid, "
              "lastmodified=:lm, version=:v "
              "WHERE  balance_id = :id";
    }

    auto prep = db.getPreparedStatement(sql);
    auto& st = prep.statement();

    st.exchange(use(balanceID, "id"));
    st.exchange(use(asset, "as"));
    st.exchange(use(balanceEntry.amount, "am"));
    st.exchange(use(balanceEntry.locked, "ld"));
    st.exchange(use(accountID, "aid"));
    st.exchange(use(balanceFrame->mEntry.lastModifiedLedgerSeq, "lm"));
    st.exchange(use(version, "v"));
    st.define_and_bind();

    auto timer = insert ? db.getInsertTimer("insert-balance")
                        : db.getUpdateTimer("update-balance");
    st.execute(true);

    if (st.get_affected_rows() != 1)
    {
        throw std::runtime_error("could not update SQL");
    }

    if (insert)
    {
        delta.addEntry(*balanceFrame);
    }
    else
    {
        delta.modEntry(*balanceFrame);
    }
}

LedgerKey
BalanceHelperImpl::getLedgerKey(LedgerEntry const& from)
{
    LedgerKey ledgerKey;
    ledgerKey.type(from.data.type());
    ledgerKey.balance().balanceID = from.data.balance().balanceID;
    return ledgerKey;
}

EntryFrame::pointer
BalanceHelperImpl::storeLoad(LedgerKey const& key)
{
    return loadBalance(key.balance().balanceID);
}

EntryFrame::pointer
BalanceHelperImpl::fromXDR(LedgerEntry const& from)
{
    return std::make_shared<BalanceFrame>(from);
}

uint64_t
BalanceHelperImpl::countObjects()
{
    uint64_t count = 0;
    getDatabase().getSession() << "SELECT COUNT(*) FROM balance;", into(count);
    return count;
}

BalanceFrame::pointer
BalanceHelperImpl::loadBalance(BalanceID balanceID)
{
    LedgerKey key;
    key.type(LedgerEntryType::BALANCE);
    key.balance().balanceID = balanceID;
    if (cachedEntryExists(key))
    {
        auto p = getCachedEntry(key);
        return p ? std::make_shared<BalanceFrame>(*p) : nullptr;
    }

    Database& db = getDatabase();

    auto balIDStrKey = BalanceKeyUtils::toStrKey(balanceID);

    std::string sql = mBalanceColumnSelector;
    sql += " WHERE balance_id = :id";
    auto prep = db.getPreparedStatement(sql);
    auto& st = prep.statement();
    st.exchange(use(balIDStrKey));

    auto timer = db.getSelectTimer("load-balance");
    BalanceFrame::pointer retBalance;
    loadBalances(prep, [&retBalance](LedgerEntry const& entry)
    {
        retBalance = make_shared<BalanceFrame>(entry);
    });

    if (retBalance == nullptr)
    {
        putCachedEntry(key, nullptr);
        return nullptr;
    }

    mStorageHelper.getLedgerDelta().recordEntry(*retBalance);
    putCachedEntry(key, make_shared<LedgerEntry>(retBalance->mEntry));

    return retBalance;
}

BalanceFrame::pointer
BalanceHelperImpl::loadBalance(BalanceID balanceID, AccountID accountID)
{
    auto balanceFrame = loadBalance(balanceID);
    if (balanceFrame == nullptr)
    {
        return nullptr;
    }

    if (balanceFrame->getAccountID() == accountID)
    {
        return balanceFrame;
    }

    return nullptr;
}

BalanceFrame::pointer
BalanceHelperImpl::loadBalance(AccountID accountID, AssetCode assetCode)
{
    Database& db = getDatabase();

    string accountIDStr = PubKeyUtils::toStrKey(accountID);

    string sql = mBalanceColumnSelector;
    sql += " WHERE asset = :asset AND account_id = :acc_id";

    auto prep = db.getPreparedStatement(sql);
    auto &st = prep.statement();
    st.exchange(use(assetCode, "asset"));
    st.exchange(use(accountIDStr, "acc_id"));

    auto timer = db.getSelectTimer("load-balances");

    BalanceFrame::pointer retBalance;
    loadBalances(prep, [&retBalance](LedgerEntry const &entry)
    {
        retBalance = make_shared<BalanceFrame>(entry);
    });

    if (retBalance == nullptr)
    {
        return nullptr;
    }

    mStorageHelper.getLedgerDelta().recordEntry(*retBalance);
    putCachedEntry(retBalance->getKey(),
                   make_shared<LedgerEntry>(retBalance->mEntry));

    return retBalance;
}

vector<BalanceFrame::pointer>
BalanceHelperImpl::loadBalances(AccountID accountID, AssetCode assetCode)
{
    Database& db = getDatabase();

    string accountIDStr = PubKeyUtils::toStrKey(accountID);

    string sql = mBalanceColumnSelector;
    sql += " WHERE asset = :asset AND account_id = :acc_id";

    auto prep = db.getPreparedStatement(sql);
    auto &st = prep.statement();
    st.exchange(use(assetCode, "asset"));
    st.exchange(use(accountIDStr, "acc_id"));

    auto timer = db.getSelectTimer("load-balances");

    std::vector<BalanceFrame::pointer> retBalances;
    loadBalances(prep, [&retBalances](LedgerEntry const &entry)
    {
        retBalances.emplace_back(make_shared<BalanceFrame>(entry));
    });

    return retBalances;
}

vector<BalanceFrame::pointer>
BalanceHelperImpl::loadBalances(vector<AccountID> accountIDs,
                                AssetCode assetCode)
{
    if (accountIDs.empty())
    {
        return vector<BalanceFrame::pointer>{};
    }

    Database& db = getDatabase();

    string sql = "SELECT DISTINCT ON (account_id) balance_id, asset, "
                 "amount, locked, account_id, lastmodified, version "
                 "FROM balance "
                 "WHERE asset = :asset AND account_id IN (" +
                 obtainStrAccountIDs(accountIDs) + ")";

    auto prep = db.getPreparedStatement(sql);
    auto &st = prep.statement();
    st.exchange(use(assetCode, "asset"));

    auto timer = db.getSelectTimer("load-balances");

    vector<BalanceFrame::pointer> result;
    loadBalances(prep, [&result](LedgerEntry const &entry)
    {
        result.emplace_back(make_shared<BalanceFrame>(entry));
    });

    return result;
}

vector<BalanceFrame::pointer>
BalanceHelperImpl::loadAssetHolders(AssetCode assetCode, AccountID owner,
                                    uint64_t minTotalAmount)
{
    Database& db = getDatabase();

    std::string ownerIDStr = PubKeyUtils::toStrKey(owner);

    std::string sql = mBalanceColumnSelector;
    sql += " WHERE asset = :asset AND account_id != :owner AND"
           " amount + locked >= :min_tot";

    auto prep = db.getPreparedStatement(sql);
    auto &st = prep.statement();
    st.exchange(use(assetCode, "asset"));
    st.exchange(use(ownerIDStr, "owner"));
    st.exchange(use(minTotalAmount, "min_tot"));

    auto timer = db.getSelectTimer("balance");

    std::vector<BalanceFrame::pointer> holders;
    loadBalances(prep, [&holders](LedgerEntry const &entry)
    {
        auto balanceFrame = make_shared<BalanceFrame>(entry);
        if (balanceFrame->getTotal() > 0)
        {
            holders.emplace_back(balanceFrame);
        }
    });

    return holders;
}

void
BalanceHelperImpl::loadBalances(StatementContext& prep,
                   function<void(LedgerEntry const&)> balanceProcessor)
{
    LedgerEntry le;
    le.data.type(LedgerEntryType::BALANCE);
    BalanceEntry& oe = le.data.balance();

    int32_t version = 0;
    string accountID, balanceID, asset;

    statement& st = prep.statement();
    st.exchange(into(balanceID));
    st.exchange(into(asset));
    st.exchange(into(oe.amount));
    st.exchange(into(oe.locked));
    st.exchange(into(accountID));
    st.exchange(into(le.lastModifiedLedgerSeq));
    st.exchange(into(version));
    st.define_and_bind();
    st.execute(true);
    while (st.got_data())
    {
        oe.accountID = PubKeyUtils::fromStrKey(accountID);
        oe.balanceID = BalanceKeyUtils::fromStrKey(balanceID);
        oe.asset = asset;
        oe.ext.v(static_cast<LedgerVersion>(version));

        bool isValid = BalanceFrame::isValid(oe);
        if (!isValid)
        {
            throw std::runtime_error("Invalid balance from database");
        }

        balanceProcessor(le);
        st.fetch();
    }
}

Database&
BalanceHelperImpl::getDatabase()
{
    return mStorageHelper.getDatabase();
}

string
BalanceHelperImpl::obtainStrAccountIDs(vector<AccountID> accountIDs)
{
    string result = "'";

    for (auto accountID : accountIDs)
    {
        result += PubKeyUtils::toStrKey(accountID);
        result += "', '";
    }

    return result.substr(0, result.size() - 3);
}

} // namespace stellar

