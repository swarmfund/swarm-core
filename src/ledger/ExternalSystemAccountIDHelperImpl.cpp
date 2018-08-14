#include "ledger/ExternalSystemAccountIDHelperImpl.h"
#include "LedgerDelta.h"
#include "crypto/Hex.h"
#include "crypto/SecretKey.h"
#include "database/Database.h"
#include "ledger/ExternalSystemAccountID.h"
#include "ledger/LedgerManager.h"
#include "ledger/StorageHelper.h"
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

const char* ExternalSystemAccountIDHelperImpl::select =
    "SELECT account_id, external_system_type, data, lastmodified, version FROM "
    "external_system_account_id";

ExternalSystemAccountIDHelperImpl::ExternalSystemAccountIDHelperImpl(
    StorageHelper& storageHelper)
    : mStorageHelper(storageHelper)
{
}

void
ExternalSystemAccountIDHelperImpl::storeUpdateHelper(const bool insert,
                                                     LedgerEntry const& entry)
{
    auto externalSystemAccountIDFrame =
        std::make_shared<ExternalSystemAccountIDFrame>(entry);
    auto externalSystemAccountIDEntry =
        externalSystemAccountIDFrame->getExternalSystemAccountID();

    externalSystemAccountIDFrame->touch(mStorageHelper.getLedgerDelta());

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

    Database& db = getDatabase();
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
        mStorageHelper.getLedgerDelta().addEntry(*externalSystemAccountIDFrame);
    }
    else
    {
        mStorageHelper.getLedgerDelta().modEntry(*externalSystemAccountIDFrame);
    }
}

void
ExternalSystemAccountIDHelperImpl::storeAdd(LedgerEntry const& entry)
{
    return storeUpdateHelper(true, entry);
}

void
ExternalSystemAccountIDHelperImpl::storeChange(LedgerEntry const& entry)
{
    return storeUpdateHelper(false, entry);
}

void
ExternalSystemAccountIDHelperImpl::storeDelete(LedgerKey const& key)
{
    Database& db = getDatabase();
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
    mStorageHelper.getLedgerDelta().deleteEntry(key);
}

void
ExternalSystemAccountIDHelperImpl::dropAll()
{
    soci::session& sess = getDatabase().getSession();
    sess << "DROP TABLE IF EXISTS external_system_account_id;";
    sess << "CREATE TABLE external_system_account_id"
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
ExternalSystemAccountIDHelperImpl::exists(LedgerKey const& key)
{
    const auto exSysAccountID = key.externalSystemAccountID();
    return exists(exSysAccountID.accountID, exSysAccountID.externalSystemType);
}

bool
ExternalSystemAccountIDHelperImpl::exists(AccountID rawAccountID,
                                          int32 externalSystemType)
{
    Database& db = getDatabase();
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
ExternalSystemAccountIDHelperImpl::getLedgerKey(LedgerEntry const& from)
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
ExternalSystemAccountIDHelperImpl::storeLoad(LedgerKey const& key)
{
    auto const& externalSystemAccountID = key.externalSystemAccountID();
    return load(externalSystemAccountID.accountID,
                externalSystemAccountID.externalSystemType);
}

EntryFrame::pointer
ExternalSystemAccountIDHelperImpl::fromXDR(LedgerEntry const& from)
{
    return std::make_shared<ExternalSystemAccountIDFrame>(from);
}

uint64_t
ExternalSystemAccountIDHelperImpl::countObjects()
{
    uint64_t count = 0;
    getDatabase().getSession()
        << "SELECT COUNT(*) FROM external_system_account_id;",
        into(count);
    return count;
}

ExternalSystemAccountIDFrame::pointer
ExternalSystemAccountIDHelperImpl::load(const AccountID rawAccountID,
                                        const int32 externalSystemType)
{
    Database& db = getDatabase();
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
        result = std::make_shared<ExternalSystemAccountIDFrame>(entry);
    });

    if (!result)
    {
        return nullptr;
    }

    mStorageHelper.getLedgerDelta().recordEntry(*result);

    return result;
}

void
ExternalSystemAccountIDHelperImpl::load(
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
ExternalSystemAccountIDHelperImpl::loadAll()
{
    std::vector<ExternalSystemAccountIDFrame::pointer>
        retExternalSystemAccountIDs;
    std::string sql = select;
    auto prep = getDatabase().getPreparedStatement(sql);

    auto timer = getDatabase().getSelectTimer("external system account id");
    load(prep, [&retExternalSystemAccountIDs](LedgerEntry const& of) {
        retExternalSystemAccountIDs.emplace_back(
            make_shared<ExternalSystemAccountIDFrame>(of));
    });
    return retExternalSystemAccountIDs;
}

Database&
ExternalSystemAccountIDHelperImpl::getDatabase()
{
    return mStorageHelper.getDatabase();
}
} // namespace stellar