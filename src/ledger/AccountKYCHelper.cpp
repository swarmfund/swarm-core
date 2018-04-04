//
// Created by volodymyr on 03.01.18.
//

#include "AccountKYCHelper.h"

namespace stellar
{

void AccountKYCHelper::dropAll(Database &db)
{
    db.getSession() << "DROP TABLE IF EXISTS account_kyc;";

    db.getSession() << "CREATE TABLE account_kyc"
                       "("
                       "accountid       VARCHAR(56)     PRIMARY KEY,"
                       "kyc_data        TEXT            NOT NULL,"
                       "lastmodified    INT             NOT NULL,"
                       "version         INT             NOT NULL    DEFAULT 0"
                       ");";
}

void AccountKYCHelper::storeUpdate(LedgerDelta &delta, Database &db, const LedgerEntry &entry, bool insert)
{
    auto accountKYCFrame = std::make_shared<AccountKYCFrame>(entry);

    accountKYCFrame->touch(delta);

    auto key = accountKYCFrame->getKey();
    flushCachedEntry(key, db);

    std::string sqlQuery;
    if (insert)
        sqlQuery = std::string("INSERT INTO account_kyc (accountid, kyc_data, lastmodified, version) "
                               "VALUES (:id, :data, :lm, :v) ");
    else
        sqlQuery = std::string("UPDATE account_kyc "
                               "SET    kyc_data=:data, lastmodified=:lm, version=:v "
                               "WHERE  accountid=:id ");

    auto accountKYCEntry = accountKYCFrame->getAccountKYC();
    std::string accIdStr = PubKeyUtils::toStrKey(accountKYCEntry.accountID);
    uint32_t version = static_cast<uint32_t>(accountKYCEntry.ext.v());

    auto prep = db.getPreparedStatement(sqlQuery);
    auto& st = prep.statement();

    st.exchange(soci::use(accIdStr, "id"));
    st.exchange(soci::use(accountKYCEntry.KYCData, "data"));
    st.exchange(soci::use(accountKYCFrame->mEntry.lastModifiedLedgerSeq, "lm"));
    st.exchange(soci::use(version, "v"));

    st.define_and_bind();

    auto timer = insert ? db.getInsertTimer("account_kyc") : db.getUpdateTimer("account_kyc");
    st.execute(true);

    if (st.get_affected_rows() != 1)
    {
        throw std::runtime_error("Could not update data in SQL");
    }

    if (insert)
        delta.addEntry(*accountKYCFrame);
    else
        delta.modEntry(*accountKYCFrame);

}

LedgerKey AccountKYCHelper::getLedgerKey(LedgerEntry const& from)
{
    LedgerKey key;
    key.type(from.data.type());
    key.accountKYC().accountID = from.data.accountKYC().accountID;
    return key;
}

void AccountKYCHelper::storeAdd(LedgerDelta &delta, Database &db, LedgerEntry const &entry)
{
    storeUpdate(delta, db, entry, true);
}

void AccountKYCHelper::storeChange(LedgerDelta &delta, Database &db, LedgerEntry const &entry)
{
    storeUpdate(delta, db, entry, false);
}

void AccountKYCHelper::storeDelete(LedgerDelta &delta, Database &db, LedgerKey const &key)
{
    flushCachedEntry(key, db);
    std::string sqlQuery = std::string("DELETE FROM account_kyc "
                                       "WHERE accountid=:id ");

    auto prep = db.getPreparedStatement(sqlQuery);
    auto& st = prep.statement();

    std::string accIdStr = PubKeyUtils::toStrKey(key.accountKYC().accountID);
    st.exchange(soci::use(accIdStr, "id"));
    st.define_and_bind();

    auto timer = db.getDeleteTimer("account_kyc");
    st.execute(true);

    delta.deleteEntry(key);
}

bool AccountKYCHelper::exists(Database &db, LedgerKey const &key)
{
    if (cachedEntryExists(key, db) && getCachedEntry(key, db) != nullptr)
        return true;

    std::string sqlQuery = std::string("SELECT EXISTS (SELECT NULL "
                                                      "FROM   account_kyc "
                                                      "WHERE  accountid=:id)");
    auto prep = db.getPreparedStatement(sqlQuery);
    auto& st = prep.statement();

    std::string accIdStr = PubKeyUtils::toStrKey(key.accountKYC().accountID);
    int exists = 0;
    st.exchange(soci::use(accIdStr, "id"));
    st.exchange(soci::into(exists));

    st.define_and_bind();
    st.execute(true);

    return exists != 0;
}

uint64_t AccountKYCHelper::countObjects(soci::session &sess)
{
    uint32_t count = 0;
    sess << "SELECT COUNT(*) FROM account_kyc;", soci::into(count);

    return count;
}

EntryFrame::pointer AccountKYCHelper::storeLoad(LedgerKey const &ledgerKey, Database &db)
{
    return loadAccountKYC(ledgerKey.accountKYC().accountID, db);
}

AccountKYCFrame::pointer AccountKYCHelper::loadAccountKYC(const AccountID &accountID, Database &db, LedgerDelta *delta)
{
    LedgerKey key;
    key.type(LedgerEntryType::ACCOUNT_KYC);
    key.accountKYC().accountID = accountID;

    if (cachedEntryExists(key, db))
    {
        auto accountKYCFrame = getCachedEntry(key, db);
        return accountKYCFrame ? std::make_shared<AccountKYCFrame>(*accountKYCFrame) : nullptr;
    }

    LedgerEntry ledgerEntry;
    ledgerEntry.data.type(LedgerEntryType::ACCOUNT_KYC);
    auto& accountKYC = ledgerEntry.data.accountKYC();
    accountKYC.accountID = accountID;

    std::string sqlQuery = std::string("SELECT kyc_data, lastmodified, version "
                                       "FROM   account_kyc "
                                       "WHERE  accountid=:id");
    auto prep = db.getPreparedStatement(sqlQuery);
    auto& st = prep.statement();

    uint32_t version(0);
    st.exchange(soci::into(accountKYC.KYCData));
    st.exchange(soci::into(ledgerEntry.lastModifiedLedgerSeq));
    st.exchange(soci::into(version));

    std::string accIdStr = PubKeyUtils::toStrKey(accountID);
    st.exchange(soci::use(accIdStr, "id"));

    st.define_and_bind();
    st.execute(true);

    if (!st.got_data())
    {
        putCachedEntry(key, nullptr, db);
        return nullptr;
    }

    accountKYC.ext.v((LedgerVersion)version);
    auto accountKYCFrame = std::make_shared<AccountKYCFrame>(ledgerEntry);

    auto pEntry = std::make_shared<const LedgerEntry>(ledgerEntry);
    putCachedEntry(key, pEntry, db);

    return accountKYCFrame;
}

EntryFrame::pointer AccountKYCHelper::fromXDR(LedgerEntry const &from)
{
    return std::make_shared<AccountKYCFrame>(from);
}


}