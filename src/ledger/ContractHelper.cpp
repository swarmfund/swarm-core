#include <lib/xdrpp/xdrpp/marshal.h>
#include <lib/util/basen.h>
#include "ContractHelper.h"
#include "LedgerDelta.h"

using namespace std;
using namespace soci;

namespace stellar
{
    const char* contractSelector = "SELECT id, contractor, customer, escrow, start_time,"
                                   "       end_time, details, status, lastmodified, version "
                                   "FROM   contracts";

    void ContractHelper::dropAll(Database &db)
    {
        db.getSession() << "DROP TABLE IF EXISTS contracts;";
        db.getSession() << "CREATE TABLE contracts"
                           "("
                           "id              BIGINT      NOT NULL CHECK (id >= 0) PRIMARY KEY,"
                           "contractor      VARCHAR(56) NOT NULL,"
                           "customer        VARCHAR(56) NOT NULL,"
                           "escrow          VARCHAR(56) NOT NULL,"
                           "disputer        VARCHAR(56) DEFAULT NULL,"
                           "start_time      BIGINT      NOT NULL CHECK (start_time >= 0),"
                           "end_time        BIGINT      NOT NULL CHECK (end_time >= 0),"
                           "details         TEXT        NOT NULL,"
                           "status          INT         NOT NULL,"
                           "lastmodified    INT         NOT NULL,"
                           "version         INT         NOT NULL DEFAULT 0"
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
        string escrowID = PubKeyUtils::toStrKey(contractEntry.escrow);
        string disputerID;
        indicator disputerIDIndicator = i_null;
        if (contractEntry.statusInfo.status() == ContractStatus::DISPUTING)
        {
            disputerID = PubKeyUtils::toStrKey(contractEntry.statusInfo.disputer());
            disputerIDIndicator = i_ok;
        }

        auto detailsBytes = xdr::xdr_to_opaque(contractEntry.details);
        string strDetails = bn::encode_b64(detailsBytes);
        auto status = static_cast<int32_t>(contractEntry.statusInfo.status());
        auto version = static_cast<int32_t>(contractEntry.ext.v());

        string sql;

        if (insert)
        {
            sql = "INSERT INTO contracts (id, contractor, customer, escrow, disputer, start_time, end_time, "
                  "                       details, status, lastmodified, version) "
                  "VALUES (:id, :contractor, :customer, :escrow, :disputer, :s_t, :e_t, :details, :status, :lm, :v)";
        }
        else
        {
            sql = "UPDATE contracts "
                  "SET    contractor = :contractor, customer = :customer, escrow = :escrow,"
                  "       disputer = :disputer, start_time = :s_t, end_time = :e_t, details = :details,"
                  "       status = :status, lastmodified = :lm, version = :v "
                  "WHERE  id = :id";
        }

        auto prep = db.getPreparedStatement(sql);
        auto& st = prep.statement();

        st.exchange(use(contractEntry.contractID, "id"));
        st.exchange(use(contractorID, "contractor"));
        st.exchange(use(customerID, "customer"));
        st.exchange(use(escrowID, "escrow"));
        st.exchange(use(disputerID, disputerIDIndicator, "disputer"));
        st.exchange(use(contractEntry.startTime, "s_t"));
        st.exchange(use(contractEntry.endTime, "e_t"));
        st.exchange(use(strDetails, "details"));
        st.exchange(use(status, "status"));
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
        auto const &contract = key.contract();
        return loadContract(contract.contractID, db);
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
        sess << "SELECT COUNT(*) FROM contracts;", into(count);
        return count;
    }

    void
    ContractHelper::load(StatementContext& prep,
                         function<void(LedgerEntry const&)> processor)
    {
        string contractorID, customerID, escrowID, disputerID, details;
        indicator disputerIDIndicator = i_null;

        LedgerEntry le;
        le.data.type(LedgerEntryType::CONTRACT);
        ContractEntry& oe = le.data.contract();
        int32_t status = 0;
        int32_t version = 0;

        auto& st = prep.statement();
        st.exchange(into(oe.contractID));
        st.exchange(into(contractorID));
        st.exchange(into(customerID));
        st.exchange(into(escrowID));
        st.exchange(into(disputerID, disputerIDIndicator));
        st.exchange(into(oe.startTime));
        st.exchange(into(oe.endTime));
        st.exchange(into(details));
        st.exchange(into(status));
        st.exchange(into(le.lastModifiedLedgerSeq));
        st.exchange(into(version));
        st.define_and_bind();
        st.execute(true);

        while (st.got_data())
        {
            oe.contractor = PubKeyUtils::fromStrKey(contractorID);
            oe.customer = PubKeyUtils::fromStrKey(customerID);
            oe.escrow = PubKeyUtils::fromStrKey(escrowID);

            std::vector<uint8_t> decoded;
            bn::decode_b64(details, decoded);
            xdr::xdr_get unmarshaler(&decoded.front(), &decoded.back() + 1);
            xdr::xdr_argpack_archive(unmarshaler, oe.details);
            unmarshaler.done();

            oe.ext.v(static_cast<LedgerVersion>(version));
            oe.statusInfo.status(static_cast<ContractStatus>(status));

            if ((oe.statusInfo.status() == ContractStatus::DISPUTING) && (disputerIDIndicator == i_ok))
                oe.statusInfo.disputer() = PubKeyUtils::fromStrKey(disputerID);

            processor(le);
            st.fetch();
        }
    }

    ContractFrame::pointer
    ContractHelper::loadContract(uint64_t id, Database &db, LedgerDelta *delta)
    {
        ContractFrame::pointer contractFrame;

        string sql = contractSelector;
        sql += " WHERE id = :id";
        auto prep = db.getPreparedStatement(sql);
        auto& st = prep.statement();
        st.exchange(use(id, "id"));

        auto timer = db.getSelectTimer("contract");
        load(prep, [&contractFrame](LedgerEntry const& contract)
        {
            contractFrame = make_shared<ContractFrame>(contract);
        });

        if (delta && contractFrame)
            delta->recordEntry(*contractFrame);

        return contractFrame;
    }
}