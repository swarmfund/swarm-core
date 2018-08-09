// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ledger/ExternalSystemAccountIDHelperLegacy.h"
#include "LedgerDelta.h"
#include "crypto/Hex.h"
#include "crypto/SecretKey.h"
#include "database/Database.h"
#include "ledger/LedgerManager.h"
#include "lib/util/format.h"
#include "util/basen.h"
#include "util/types.h"
#include "xdrpp/printer.h"
#include <algorithm>

using namespace soci;
using namespace std;

namespace stellar
{
using xdr::operator<;

const char* ExternalSystemAccountIDHelperLegacy::select =
    "SELECT account_id, external_system_type, data, lastmodified, version FROM "
    "external_system_account_id";

void
ExternalSystemAccountIDHelperLegacy::storeUpdateHelper(LedgerDelta& delta,
                                                       Database& db,
                                                       const bool insert,
                                                       LedgerEntry const& entry)
{
    auto externalSystemAccountIDFrame =
        std::make_shared<ExternalSystemAccountIDFrame>(entry);
    auto externalSystemAccountIDEntry =
        externalSystemAccountIDFrame->getExternalSystemAccountID();

    externalSystemAccountIDFrame->touch(delta);

    bool isValid = externalSystemAccountIDFrame->isValid();
    if (!isValid)
    {
        CLOG(ERROR, Logging::ENTRY_LOGGER)
            << "Unexpected state: trying to insert/update invalid external "
               "system account id: "
            << xdr::xdr_to_string(externalSystemAccountIDEntry);
        throw runtime_error(
            "Unexpected state: invalid external system account id");
    }

    string sql;

    if (insert)
    {
        sql = "INSERT INTO external_system_account_id (account_id, "
              "external_system_type, data, lastmodified, version) "
              "VALUES (:id, :ex_sys_type, :data, :lm, :v)";
    }
    else
    {
        sql = "UPDATE external_system_account_id "
              "SET data = :data, lastmodified=:lm, version=:v "
              "WHERE account_id = :id AND external_system_type = :ex_sys_type";
    }

    auto prep = db.getPreparedStatement(sql);
    auto& st = prep.statement();

    auto accountID =
        PubKeyUtils::toStrKey(externalSystemAccountIDEntry.accountID);
    st.exchange(use(accountID, "id"));
    st.exchange(
        use(externalSystemAccountIDEntry.externalSystemType, "ex_sys_type"));
    st.exchange(use(externalSystemAccountIDEntry.data, "data"));
    st.exchange(
        use(externalSystemAccountIDFrame->mEntry.lastModifiedLedgerSeq, "lm"));
    const auto version =
        static_cast<int32_t>(externalSystemAccountIDEntry.ext.v());
    st.exchange(use(version, "v"));
    st.define_and_bind();

    auto timer = insert ? db.getInsertTimer("external-system-account-id")
                        : db.getUpdateTimer("external-system-account-id");
    st.execute(true);

    if (st.get_affected_rows() != 1)
    {
        throw runtime_error("could not update SQL");
    }

    if (insert)
    {
        delta.addEntry(*externalSystemAccountIDFrame);
    }
    else
    {
        delta.modEntry(*externalSystemAccountIDFrame);
    }
}

void
ExternalSystemAccountIDHelperLegacy::storeAdd(LedgerDelta& delta, Database& db,
                                              LedgerEntry const& entry)
{
    return storeUpdateHelper(delta, db, true, entry);
}

void
ExternalSystemAccountIDHelperLegacy::storeChange(LedgerDelta& delta,
                                                 Database& db,
                                                 LedgerEntry const& entry)
{
    return storeUpdateHelper(delta, db, false, entry);
}

void
ExternalSystemAccountIDHelperLegacy::storeDelete(LedgerDelta& delta,
                                                 Database& db,
                                                 LedgerKey const& key)
{
    auto timer = db.getDeleteTimer("external_system_account_id");
    auto prep = db.getPreparedStatement(
        "DELETE FROM external_system_account_id WHERE account_id=:id AND "
        "external_system_type=:etype");
    auto& st = prep.statement();
    const auto exSysAccountID = key.externalSystemAccountID();
    auto accountID = PubKeyUtils::toStrKey(exSysAccountID.accountID);
    st.exchange(use(accountID, "id"));
    st.exchange(use(exSysAccountID.externalSystemType, "etype"));
    st.define_and_bind();
    st.execute(true);
    delta.deleteEntry(key);
}

void
ExternalSystemAccountIDHelperLegacy::dropAll(Database& db)
{
    db.getSession() << "DROP TABLE IF EXISTS external_system_account_id;";
    db.getSession() << "CREATE TABLE external_system_account_id"
                       "("
                       "account_id           VARCHAR(56) NOT NULL,"
                       "external_system_type INT         NOT NULL,"
                       "data                 TEXT        NOT NULL,"
                       "lastmodified         INT         NOT NULL, "
                       "version              INT         NOT NULL DEFAULT 0,"
                       "PRIMARY KEY (account_id, external_system_type)"
                       ");";
}

bool
ExternalSystemAccountIDHelperLegacy::exists(Database& db, LedgerKey const& key)
{
    const auto exSysAccountID = key.externalSystemAccountID();
    return exists(db, exSysAccountID.accountID,
                  exSysAccountID.externalSystemType);
}

bool
ExternalSystemAccountIDHelperLegacy::exists(Database& db,
                                            AccountID rawAccountID,
                                            int32 externalSystemType)
{
    int exists = 0;
    auto timer = db.getSelectTimer("external-system-account-id-exists");
    auto prep = db.getPreparedStatement(
        "SELECT EXISTS (SELECT NULL FROM external_system_account_id WHERE "
        "account_id=:id AND external_system_type = :ex_sys_type)");
    auto& st = prep.statement();
    auto accountID = PubKeyUtils::toStrKey(rawAccountID);
    st.exchange(use(accountID, "id"));
    st.exchange(use(externalSystemType, "ex_sys_type"));
    st.exchange(into(exists));
    st.define_and_bind();
    st.execute(true);

    return exists != 0;
}

LedgerKey
ExternalSystemAccountIDHelperLegacy::getLedgerKey(LedgerEntry const& from)
{
    LedgerKey ledgerKey;
    ledgerKey.type(from.data.type());
    ledgerKey.externalSystemAccountID().accountID =
        from.data.externalSystemAccountID().accountID;
    ledgerKey.externalSystemAccountID().externalSystemType =
        from.data.externalSystemAccountID().externalSystemType;
    return ledgerKey;
}

EntryFrame::pointer
ExternalSystemAccountIDHelperLegacy::storeLoad(LedgerKey const& key,
                                               Database& db)
{
    auto const& externalSystemAccountID = key.externalSystemAccountID();
    return load(externalSystemAccountID.accountID,
                externalSystemAccountID.externalSystemType, db);
}

EntryFrame::pointer
ExternalSystemAccountIDHelperLegacy::fromXDR(LedgerEntry const& from)
{
    return std::make_shared<ExternalSystemAccountIDFrame>(from);
}

uint64_t
ExternalSystemAccountIDHelperLegacy::countObjects(soci::session& sess)
{
    uint64_t count = 0;
    sess << "SELECT COUNT(*) FROM external_system_account_id;", into(count);
    return count;
}

ExternalSystemAccountIDFrame::pointer
ExternalSystemAccountIDHelperLegacy::load(const AccountID rawAccountID,
                                          const int32 externalSystemType,
                                          Database& db, LedgerDelta* delta)
{
    std::string sql = select;
    sql += +" WHERE account_id = :id AND external_system_type = :ex_sys_type";
    auto prep = db.getPreparedStatement(sql);
    auto& st = prep.statement();
    auto accountID = PubKeyUtils::toStrKey(rawAccountID);
    st.exchange(use(accountID, "id"));
    st.exchange(use(externalSystemType, "ex_sys_type"));

    ExternalSystemAccountIDFrame::pointer result;
    auto timer = db.getSelectTimer("external-system-account");
    load(prep, [&result](LedgerEntry const& entry) {
        result = make_shared<ExternalSystemAccountIDFrame>(entry);
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

void
ExternalSystemAccountIDHelperLegacy::load(
    StatementContext& prep, const function<void(LedgerEntry const&)> processor)
{
    LedgerEntry le;
    le.data.type(LedgerEntryType::EXTERNAL_SYSTEM_ACCOUNT_ID);
    auto& oe = le.data.externalSystemAccountID();
    int version;

    statement& st = prep.statement();
    st.exchange(into(oe.accountID));
    st.exchange(into(oe.externalSystemType));
    st.exchange(into(oe.data));
    st.exchange(into(le.lastModifiedLedgerSeq));
    st.exchange(into(version));
    st.define_and_bind();
    st.execute(true);

    while (st.got_data())
    {
        oe.ext.v(static_cast<LedgerVersion>(version));

        bool isValid = ExternalSystemAccountIDFrame::isValid(oe);
        if (!isValid)
        {
            CLOG(ERROR, Logging::ENTRY_LOGGER)
                << "Unexpected state: loaded invalid external system account "
                   "id: "
                << xdr::xdr_to_string(oe);
            throw runtime_error("Loaded invalid external system account id");
        }

        processor(le);
        st.fetch();
    }
}

std::vector<ExternalSystemAccountIDFrame::pointer>
ExternalSystemAccountIDHelperLegacy::loadAll(Database& db)
{
    std::vector<ExternalSystemAccountIDFrame::pointer>
        retExternalSystemAccountIDs;
    std::string sql = select;
    auto prep = db.getPreparedStatement(sql);

    auto timer = db.getSelectTimer("external system account id");
    load(prep, [&retExternalSystemAccountIDs](LedgerEntry const& of) {
        retExternalSystemAccountIDs.emplace_back(
            make_shared<ExternalSystemAccountIDFrame>(of));
    });
    return retExternalSystemAccountIDs;
}
} // namespace stellar