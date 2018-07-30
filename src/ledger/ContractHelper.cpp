#include <lib/xdrpp/xdrpp/marshal.h>
#include <lib/util/basen.h>
#include "ContractHelper.h"
#include "LedgerDelta.h"

using namespace std;
using namespace soci;

namespace stellar
{
    const char* contractSelector = "";

    void ContractHelper::dropAll(Database &db)
    {
        db.getSession() << "DROP TABLE IF EXISTS contracts;";
        db.getSession() << "CREATE TABLE contracts"
                           "("
                           "id              BIGINT      NOT NULL CHECK (id >= 0) PRIMARY KEY,"
                           "contractor      VARCHAR(56) NOT NULL,"
                           "customer        VARCHAR(56) NOT NULL,"
                           "judge           VARCHAR(56) NOT NULL,"
                           "invoices        TEXT        NOT NULL,"
                           "start_time      BIGINT      NOT NULL CHECK (start_time >= 0),"
                           "end_time        BIGINT      NOT NULL CHECK (end_time >= 0),"
                           "details         TEXT        NOT NULL,"
                           "lastmodified    INT         NOT NULL,"
                           "version         INT         NOT NULL"
                           ");";
    }

    void
    ContractHelper::storeUpdateHelper(LedgerDelta &delta, Database &db, bool insert, LedgerEntry const &entry)
    {
        auto contractFrame = make_shared<ContractFrame>(entry);
        auto contractEntry = contractFrame->getContract();

        contractFrame->touch(delta);

        auto key = contractFrame->getKey();
        flushCachedEntry(key, db);

        string contractorID = PubKeyUtils::toStrKey(contractEntry.contractor);
        string customerID = PubKeyUtils::toStrKey(contractEntry.customer);
        string judgeID = PubKeyUtils::toStrKey(contractEntry.judge);
        auto invoicesBytes = xdr::xdr_to_opaque(contractEntry.invoiceRequestIDs);
        string strInvoices = bn::encode_b64(invoicesBytes);
        auto version = static_cast<int32_t>(contractEntry.ext.v());

        string sql;

        if (insert)
        {
            sql = "INSERT INTO contracts (id, contractor, customer, judge, invoices, start_time, end_time, "
                  "                       details, lastmodified, version) "
                  "VALUES (:id, :contractor, :customer, :judge, :invoices, :s_t, :e_t, :details, :lm, :v)";
        }
        else
        {
            sql = "UPDATE contracts "
                  "SET    contractor = :contractor, customer = :customer, judge = :judge,"
                  "       invoices = :invoices, start_time = :s_t, end_time = :e_t,"
                  "       details = :details, lastmodified = :lm, version = :v "
                  "WHERE  id = :id";
        }

        auto prep = db.getPreparedStatement(sql);
        auto& st = prep.statement();

        st.exchange(use(contractEntry.contractID, "id"));
        st.exchange(use(contractorID, "contractor"));
        st.exchange(use(customerID, "customer"));
        st.exchange(use(judgeID, "judge"));
        st.exchange(use(strInvoices, "invoices"));
        st.exchange(use(contractEntry.startTime, "s_t"));
        st.exchange(use(contractEntry.endTime, "e_t"));
        st.exchange(use(contractEntry.details, "details"));
        st.exchange(use(contractFrame->getLastModified(), "lm"));
        st.exchange(use(version, "v"));
        st.define_and_bind();

        auto timer = insert ? db.getInsertTimer("contracts")
                            : db.getUpdateTimer("contracts");
        st.execute(true);

        if (st.get_affected_rows() != 1)
            throw runtime_error("could not update SQL");

        if (insert)
            delta.addEntry(*contractFrame);
        else
            delta.modEntry(*contractFrame);
    }

    void
    ContractHelper::storeAdd(LedgerDelta &delta, Database &db, LedgerEntry const &entry)
    {
        storeUpdateHelper(delta, db, true, entry);
    }

    void
    ContractHelper::storeChange(LedgerDelta &delta, Database &db, LedgerEntry const &entry)
    {
        storeUpdateHelper(delta, db, false, entry);
    }

    void
    ContractHelper::storeDelete(LedgerDelta& delta, Database& db, LedgerKey const &key)
    {
        auto timer = db.getDeleteTimer("contracts");
        auto prep = db.getPreparedStatement("DELETE FROM contracts WHERE id = :id");
        auto& st = prep.statement();
        st.exchange(use(key.contract().contractID));
        st.define_and_bind();
        st.execute(true);
        delta.deleteEntry(key);
    }

    bool
    ContractHelper::exists(Database& db, LedgerKey const &key)
    {
        int exists = 0;
        auto timer = db.getSelectTimer("contract_exists");
        auto prep = db.getPreparedStatement("SELECT EXISTS (SELECT NULL FROM contracts WHERE id = :id)");
        auto& st = prep.statement();
        st.exchange(use(key.contract().contractID, "id"));
        st.exchange(into(exists));
        st.define_and_bind();
        st.execute(true);

        return exists != 0;
    }

    LedgerKey
    ContractHelper::getLedgerKey(LedgerEntry const &from)
    {
        LedgerKey ledgerKey;
        ledgerKey.type(from.data.type());
        ledgerKey.contract().contractID = from.data.contract().contractID;
        return ledgerKey;
    }

    EntryFrame::pointer
    ContractHelper::storeLoad(LedgerKey const &key, Database &db)
    {

    }

    EntryFrame::pointer
    ContractHelper::fromXDR(LedgerEntry const &from)
    {
        return make_shared<ContractFrame>(from);
    }

    uint64_t
    ContractHelper::countObjects(soci::session &sess)
    {
        uint64_t count = 0;
        sess << "SELECT COUNT(*) FROM contracts;" into(count);
        return count;
    }

    void
    ContractHelper::load(StatementContext& prep,
                         function<void(LedgerEntry const&)> balanceProcessor)
    {
        string contractorID, customerID, judgeID;

        LedgerEntry le;
        le.data.type(LedgerEntryType::CONTRACT);
        ContractEntry& oe = le.data.contract();
        int32_t version = 0;

        auto& st = prep.statement();
        st.exchange(into())
    }

    ContractFrame::pointer
    ContractHelper::loadContract(uint64_t id, Database &db, LedgerDelta *delta)
    {

    }
}