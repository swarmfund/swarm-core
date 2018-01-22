// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "BalanceHelper.h"
#include "crypto/SecretKey.h"
#include "crypto/Hex.h"
#include "database/Database.h"
#include "LedgerDelta.h"
#include "ledger/LedgerManager.h"
#include "util/basen.h"
#include "util/types.h"
#include "lib/util/format.h"
#include <algorithm>

using namespace soci;
using namespace std;

namespace stellar {
    using xdr::operator<;

    static const char *balanceColumnSelector =
            "SELECT balance_id, asset, amount, locked, account_id, lastmodified, version "
                    "FROM balance";

    void
    BalanceHelper::dropAll(Database &db) {
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
    BalanceHelper::storeUpdateHelper(LedgerDelta &delta, Database &db, bool insert, LedgerEntry const &entry) {
        auto balanceFrame = make_shared<BalanceFrame>(entry);
        auto balanceEntry = balanceFrame->getBalance();

        balanceFrame->touch(delta);

        bool isValid = balanceFrame->isValid();
        if (!isValid) {
            throw std::runtime_error("Invalid balance");
        }

        std::string accountID = PubKeyUtils::toStrKey(balanceFrame->getAccountID());
        std::string balanceID = BalanceKeyUtils::toStrKey(balanceFrame->getBalanceID());
        std::string asset = balanceFrame->getAsset();
        int32_t balanceVersion = static_cast<int32_t >(balanceFrame->getBalance().ext.v());

        string sql;

        if (insert) {
            sql = "INSERT INTO balance (balance_id, asset, amount, locked, account_id, "
                    " lastmodified, version) "
                    "VALUES (:id, :as, :am, :ld, :aid, :lm, :v)";
        } else {
            sql = "UPDATE balance "
                    "SET    asset = :as, amount=:am, locked=:ld, account_id=:aid, "
                    "lastmodified=:lm, version=:v "
                    "WHERE  balance_id = :id";
        }

        auto prep = db.getPreparedStatement(sql);
        auto &st = prep.statement();


        st.exchange(use(balanceID, "id"));
        st.exchange(use(asset, "as"));
        st.exchange(use(balanceEntry.amount, "am"));
        st.exchange(use(balanceEntry.locked, "ld"));
        st.exchange(use(accountID, "aid"));
        st.exchange(use(balanceFrame->mEntry.lastModifiedLedgerSeq, "lm"));
        st.exchange(use(balanceVersion, "v"));
        st.define_and_bind();

        auto timer =
                insert ? db.getInsertTimer("balance") : db.getUpdateTimer("balance");
        st.execute(true);

        if (st.get_affected_rows() != 1) {
            throw std::runtime_error("could not update SQL");
        }

        if (insert) {
            delta.addEntry(*balanceFrame);
        } else {
            delta.modEntry(*balanceFrame);
        }

    }

    void
    BalanceHelper::storeAdd(LedgerDelta &delta, Database &db, LedgerEntry const &entry) {
        storeUpdateHelper(delta, db, true, entry);
    }

    void
    BalanceHelper::storeChange(LedgerDelta &delta, Database &db, LedgerEntry const &entry) {
        storeUpdateHelper(delta, db, false, entry);
    }

    void
    BalanceHelper::storeDelete(LedgerDelta &delta, Database &db, LedgerKey const &key) {
        auto timer = db.getDeleteTimer("balance");
        auto prep = db.getPreparedStatement("DELETE FROM balance WHERE balance_id=:id");
        auto &st = prep.statement();
        auto balanceID = key.balance().balanceID;
        auto balIDStrKey = BalanceKeyUtils::toStrKey(balanceID);
        st.exchange(use(balIDStrKey));
        st.define_and_bind();
        st.execute(true);
        delta.deleteEntry(key);
    }

    bool
    BalanceHelper::exists(Database &db, LedgerKey const &key) {
        int exists = 0;
        auto timer = db.getSelectTimer("balance-exists");
        auto prep =
                db.getPreparedStatement("SELECT EXISTS (SELECT NULL FROM balance WHERE balance_id=:id)");
        auto &st = prep.statement();
        auto balanceID = key.balance().balanceID;
        auto balIDStrKey = BalanceKeyUtils::toStrKey(balanceID);
        st.exchange(use(balIDStrKey));
        st.exchange(into(exists));
        st.define_and_bind();
        st.execute(true);

        return exists != 0;
    }

    LedgerKey
    BalanceHelper::getLedgerKey(LedgerEntry const &from) {
        LedgerKey ledgerKey;
        ledgerKey.type(from.data.type());
        ledgerKey.balance().balanceID = from.data.balance().balanceID;
        return ledgerKey;
    }

    EntryFrame::pointer
    BalanceHelper::storeLoad(LedgerKey const &key, Database &db) {
        auto const &balance = key.balance();
        return loadBalance(balance.balanceID, db);
    }

    EntryFrame::pointer
    BalanceHelper::fromXDR(LedgerEntry const &from) {
        return std::make_shared<BalanceFrame>(from);
    }

    uint64_t
    BalanceHelper::countObjects(soci::session &sess) {
        uint64_t count = 0;
        sess << "SELECT COUNT(*) FROM balance;", into(count);
        return count;
    }

    BalanceFrame::pointer
    BalanceHelper::loadBalance(BalanceID balanceID, Database &db,
                               LedgerDelta *delta) {
        BalanceFrame::pointer retBalance;
        auto balIDStrKey = BalanceKeyUtils::toStrKey(balanceID);

        std::string sql = balanceColumnSelector;
        sql += " WHERE balance_id = :id";
        auto prep = db.getPreparedStatement(sql);
        auto &st = prep.statement();
        st.exchange(use(balIDStrKey));

        auto timer = db.getSelectTimer("balance");
        loadBalances(prep, [&retBalance](LedgerEntry const &Balance) {
            retBalance = make_shared<BalanceFrame>(Balance);
        });

        if (delta && retBalance) {
            delta->recordEntry(*retBalance);
        }

        return retBalance;
    }

    BalanceFrame::pointer
    BalanceHelper::loadBalance(AccountID account, AssetCode asset, Database &db,
                               LedgerDelta *delta) {
        BalanceFrame::pointer retBalance;
        string actIDStrKey;
        string assetCode = asset;

        actIDStrKey = PubKeyUtils::toStrKey(account);
        std::string sql = balanceColumnSelector;
        sql += " WHERE account_id = :aid AND asset = :as ORDER BY balance_id DESC LIMIT 1";
        auto prep = db.getPreparedStatement(sql);
        auto &st = prep.statement();
        st.exchange(use(actIDStrKey));
        st.exchange(use(assetCode));

        auto timer = db.getSelectTimer("balance");
        loadBalances(prep, [&retBalance](LedgerEntry const &Balance) {
            retBalance = make_shared<BalanceFrame>(Balance);
        });

        if (delta && retBalance) {
            delta->recordEntry(*retBalance);
        }

        return retBalance;
    }

    void
    BalanceHelper::loadBalances(StatementContext &prep,
                                std::function<void(LedgerEntry const &)> balanceProcessor) {
        string accountID, balanceID, asset;

        LedgerEntry le;
        le.data.type(LedgerEntryType::BALANCE);
        BalanceEntry &oe = le.data.balance();
        int32_t balanceVersion = 0;

        // SELECT balance_id, asset, amount, locked, account_id, lastmodified

        statement &st = prep.statement();
        st.exchange(into(balanceID));
        st.exchange(into(asset));
        st.exchange(into(oe.amount));
        st.exchange(into(oe.locked));
        st.exchange(into(accountID));
        st.exchange(into(le.lastModifiedLedgerSeq));
        st.exchange(into(balanceVersion));
        st.define_and_bind();
        st.execute(true);
        while (st.got_data()) {
            oe.accountID = PubKeyUtils::fromStrKey(accountID);
            oe.balanceID = BalanceKeyUtils::fromStrKey(balanceID);
            oe.asset = asset;
            oe.ext.v((LedgerVersion) balanceVersion);

            bool isValid = BalanceFrame::ensureValid(oe);
            if (!isValid) {
                throw std::runtime_error("Invalid Recovery request");
            }

            balanceProcessor(le);
            st.fetch();
        }
    }

    void
    BalanceHelper::loadBalances(AccountID const &accountID,
                                std::vector<BalanceFrame::pointer> &retBalances,
                                Database &db) {
        std::string actIDStrKey;
        actIDStrKey = PubKeyUtils::toStrKey(accountID);

        std::string sql = balanceColumnSelector;
        sql += " WHERE account_id = :account_id";
        auto prep = db.getPreparedStatement(sql);
        auto &st = prep.statement();
        st.exchange(use(actIDStrKey));

        auto timer = db.getSelectTimer("balance");
        loadBalances(prep, [&retBalances](LedgerEntry const &of) {
            retBalances.emplace_back(make_shared<BalanceFrame>(of));
        });
    }

    void
    BalanceHelper::loadAssetHolders(AssetCode assetCode,
                                    std::vector<BalanceFrame::pointer> &holders,
                                    Database &db) {
        std::string sql = balanceColumnSelector;
        sql += " WHERE asset = :asset";
        auto prep = db.getPreparedStatement(sql);
        auto &st = prep.statement();
        st.exchange(use(assetCode));

        auto timer = db.getSelectTimer("balance");
        loadBalances(prep, [&holders](LedgerEntry const &of) {
            auto balanceFrame = make_shared<BalanceFrame>(of);
            if (balanceFrame->getAmount() + balanceFrame->getLocked() > 0)
                holders.emplace_back(make_shared<BalanceFrame>(of));
        });
    }

    std::unordered_map<string, BalanceFrame::pointer>
    BalanceHelper::loadBalances(AccountID const &accountID, Database &db) {
        std::unordered_map<string, BalanceFrame::pointer> retBalances;
        std::string actIDStrKey, rawAsset;
        actIDStrKey = PubKeyUtils::toStrKey(accountID);

        std::string sql = balanceColumnSelector;
        sql += " WHERE account_id = :account_id";
        auto prep = db.getPreparedStatement(sql);
        auto &st = prep.statement();
        st.exchange(use(actIDStrKey));

        auto timer = db.getSelectTimer("balance");
        loadBalances(prep, [&retBalances](LedgerEntry const &of) {
            retBalances[of.data.balance().asset] = make_shared<BalanceFrame>(of);
        });
        return retBalances;
    }

    std::unordered_map<AccountID, std::vector<BalanceFrame::pointer>>
    BalanceHelper::loadAllBalances(Database &db) {
        std::unordered_map<AccountID, std::vector<BalanceFrame::pointer>> retBalances;
        std::string sql = balanceColumnSelector;
        sql += " ORDER BY account_id";
        auto prep = db.getPreparedStatement(sql);

        auto timer = db.getSelectTimer("balance");
        loadBalances(prep, [&retBalances](LedgerEntry const &of) {
            auto &thisUserBalance = retBalances[of.data.balance().accountID];
            thisUserBalance.emplace_back(make_shared<BalanceFrame>(of));
        });
        return retBalances;
    }

    bool
    BalanceHelper::exists(Database &db, BalanceID balanceID) {
        int exists = 0;
        auto timer = db.getSelectTimer("balance-exists");
        auto prep =
                db.getPreparedStatement("SELECT EXISTS (SELECT NULL FROM balance WHERE balance_id=:id)");
        auto balIDStrKey = BalanceKeyUtils::toStrKey(balanceID);
        auto &st = prep.statement();
        st.exchange(use(balIDStrKey));
        st.exchange(into(exists));
        st.define_and_bind();
        st.execute(true);

        return exists != 0;
    }

}