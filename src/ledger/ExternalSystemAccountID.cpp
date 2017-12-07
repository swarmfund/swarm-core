// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "crypto/SecretKey.h"
#include "crypto/Hex.h"
#include "database/Database.h"
#include "LedgerDelta.h"
#include "ledger/LedgerManager.h"
#include "ledger/ExternalSystemAccountID.h"
#include "util/basen.h"
#include "util/types.h"
#include "lib/util/format.h"
#include "xdrpp/printer.h"
#include <algorithm>

using namespace soci;
using namespace std;

namespace stellar
{
using xdr::operator<;

ExternalSystemAccountIDFrame::
ExternalSystemAccountIDFrame() : EntryFrame(LedgerEntryType::
                                            EXTERNAL_SYSTEM_ACCOUNT_ID)
                               , mExternalSystemAccountID(mEntry.data.
                                                                 externalSystemAccountID())
{
}

ExternalSystemAccountIDFrame::ExternalSystemAccountIDFrame(
    LedgerEntry const& from)
    : EntryFrame(from)
    , mExternalSystemAccountID(mEntry.data.externalSystemAccountID())
{
}

ExternalSystemAccountIDFrame::ExternalSystemAccountIDFrame(
    ExternalSystemAccountIDFrame const&
    from) : ExternalSystemAccountIDFrame(from.mEntry)
{
}

ExternalSystemAccountIDFrame& ExternalSystemAccountIDFrame::operator=(
    ExternalSystemAccountIDFrame const& other)
{
    if (&other != this)
    {
        mExternalSystemAccountID = other.mExternalSystemAccountID;
        mKey = other.mKey;
        mKeyCalculated = other.mKeyCalculated;
    }
    return *this;
}

ExternalSystemAccountIDFrame::pointer ExternalSystemAccountIDFrame::createNew(
    AccountID const accountID, ExternalSystemType const externalSystemType,
    string const data)
{
    LedgerEntry le;
    le.data.type(LedgerEntryType::EXTERNAL_SYSTEM_ACCOUNT_ID);
    auto& externalSystemAccountID = le.data.externalSystemAccountID();
    externalSystemAccountID.accountID = accountID;
    externalSystemAccountID.externalSystemType = externalSystemType;
    externalSystemAccountID.data = data;
    return std::make_shared<ExternalSystemAccountIDFrame>(le);
}

bool ExternalSystemAccountIDFrame::isValid(ExternalSystemAccountID const& oe)
{
    return isValidEnumValue(oe.externalSystemType) && !oe.data.empty();
}

bool ExternalSystemAccountIDFrame::isValid() const
{
    return isValid(mExternalSystemAccountID);
}

void ExternalSystemAccountIDFrame::storeDelete(LedgerDelta& delta, Database& db) const
{
    return storeDelete(delta, db, getKey());
}

void ExternalSystemAccountIDFrame::storeChange(LedgerDelta& delta, Database& db)
{
    return storeUpdateHelper(delta, db, false);
}

void ExternalSystemAccountIDFrame::storeAdd(LedgerDelta& delta, Database& db)
{
    return storeUpdateHelper(delta, db, true);
}

void ExternalSystemAccountIDFrame::storeDelete(LedgerDelta& delta, Database& db,
    LedgerKey const& key)
{
    auto timer = db.getDeleteTimer("external_system_account_id");
    auto prep = db.getPreparedStatement("DELETE FROM external_system_account_id WHERE account_id=:id AND external_system_type=:etype");
    auto& st = prep.statement();
    const auto exSysAccountID = key.externalSystemAccountID();
    st.exchange(use(exSysAccountID.accountID, "id"));
    st.exchange(use(exSysAccountID.externalSystemType, "etype"));
    st.define_and_bind();
    st.execute(true);
    delta.deleteEntry(key);
}

bool ExternalSystemAccountIDFrame::exists(Database& db, LedgerKey const& key)
{
    const auto exSysAccountID = key.externalSystemAccountID();
    return exists(db, exSysAccountID.accountID, exSysAccountID.externalSystemType);
}

bool ExternalSystemAccountIDFrame::exists(Database& db, AccountID accountID,
    ExternalSystemType externalSystemType)
{
    int exists = 0;
    auto timer = db.getSelectTimer("external-system-account-id-exists");
    auto prep =
        db.getPreparedStatement("SELECT EXISTS (SELECT NULL FROM external_system_account_id WHERE account_id=:id AND external_system_type = :ex_sys_type)");
    auto& st = prep.statement();
    st.exchange(use(accountID, "id"));
    st.exchange(use(externalSystemType, "ex_sys_type"));
    st.exchange(into(exists));
    st.define_and_bind();
    st.execute(true);

    return exists != 0;
}

uint64_t ExternalSystemAccountIDFrame::countObjects(soci::session& sess)
{
    uint64_t count = 0;
    sess << "SELECT COUNT(*) FROM external_system_account_id;", into(count);
    return count;
}

void ExternalSystemAccountIDFrame::dropAll(Database& db)
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

const char* ExternalSystemAccountIDFrame::select = "SELECT account_id, external_system_type, data, lastmodified, version FROM external_system_account_id";

ExternalSystemAccountIDFrame::pointer ExternalSystemAccountIDFrame::load(
    const AccountID accountID, const ExternalSystemType externalSystemType,
    Database& db, LedgerDelta* delta)
{
    std::string sql = select;
    sql += +" WHERE account_id = :id AND external_system_type = :ex_sys_type";
    auto prep = db.getPreparedStatement(sql);
    auto& st = prep.statement();
    st.exchange(use(accountID, "id"));
    st.exchange(use(externalSystemType, "ex_sys_type"));

    pointer result;
    auto timer = db.getSelectTimer("external-system-account");
    load(prep, [&result](LedgerEntry const& entry)
    {
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

void ExternalSystemAccountIDFrame::storeUpdateHelper(LedgerDelta& delta,
    Database& db, const bool insert)
{
    touch(delta);

    if (!isValid())
    {
        CLOG(ERROR, Logging::ENTRY_LOGGER) << "Unexpected state: trying to insert/update invalid external system account id: " 
            << xdr::xdr_to_string(mExternalSystemAccountID);
        throw runtime_error("Unexpected state: invalid external system account id");
    }

    string sql;

    if (insert)
    {
        sql = "INSERT INTO external_system_account_id (account_id, external_system_type, data, lastmodified, version) "
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


    st.exchange(use(mExternalSystemAccountID.accountID, "id"));
    st.exchange(use(mExternalSystemAccountID.externalSystemType, "ex_sys_type"));
    st.exchange(use(mExternalSystemAccountID.data, "data"));
    st.exchange(use(mEntry.lastModifiedLedgerSeq, "lm"));
    const auto version = static_cast<int32_t>(mExternalSystemAccountID.ext.v());
    st.exchange(use(version, "v"));
    st.define_and_bind();

    auto timer =
        insert ? db.getInsertTimer("external-system-account-id") : db.getUpdateTimer("external-system-account-id");
    st.execute(true);

    if (st.get_affected_rows() != 1)
    {
        throw runtime_error("could not update SQL");
    }

    if (insert)
    {
        delta.addEntry(*this);
    }
    else
    {
        delta.modEntry(*this);
    }
}

void ExternalSystemAccountIDFrame::load(StatementContext& prep, const function<void(LedgerEntry const&)> processor)
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
        if (!isValid(oe))
        {
            CLOG(ERROR, Logging::ENTRY_LOGGER) << "Unexpected state: loaded invalid external system account id: " << xdr::xdr_to_string(oe);
            throw runtime_error("Loaded invalid external system account id");
        }

        processor(le);
        st.fetch();
    }
}
}

