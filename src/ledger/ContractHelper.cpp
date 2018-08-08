#include <lib/xdrpp/xdrpp/marshal.h>
#include <lib/util/basen.h>
#include "ContractHelper.h"
#include "LedgerDelta.h"

using namespace std;
using namespace soci;

namespace stellar
{
    const char* contractSelector = "SELECT id, contractor, customer, escrow, disputer, start_time,"
                                   "       end_time, details, dispute_reason, state, lastmodified, version "
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
                           "dispute_reason  TEXT        DEFAULT NULL,"
                           "state           INT         NOT NULL,"
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
        string disputeReason;
        indicator disputeIndicator = i_null;
        if (contractEntry.stateInfo.state() == ContractState::DISPUTING)
        {
            disputerID = PubKeyUtils::toStrKey(contractEntry.stateInfo.disputeDetails().disputer);
            disputeReason = bn::encode_b64(contractEntry.stateInfo.disputeDetails().reason);
            disputeIndicator = i_ok;
        }

        auto detailsBytes = xdr::xdr_to_opaque(contractEntry.details);
        string strDetails = bn::encode_b64(detailsBytes);
        auto state = static_cast<int32_t>(contractEntry.stateInfo.state());
        auto version = static_cast<int32_t>(contractEntry.ext.v());

        string sql;

        if (insert)
        {
            sql = "INSERT INTO contracts (id, contractor, customer, escrow, disputer, start_time, end_time, "
                  "                       details, dispute_reason, state, lastmodified, version) "
                  "VALUES (:id, :contractor, :customer, :escrow, :disputer, :s_t, :e_t,"
                  "        :details, :dis_reason, :state, :lm, :v)";
        }
        else
        {
            sql = "UPDATE contracts "
                  "SET    contractor = :contractor, customer = :customer, escrow = :escrow,"
                  "       disputer = :disputer, start_time = :s_t, end_time = :e_t, details = :details,"
                  "       dispute_reason = :dis_reason,"
                  "       state = :state, lastmodified = :lm, version = :v "
                  "WHERE  id = :id";
        }

        auto prep = db.getPreparedStatement(sql);
        auto& st = prep.statement();

        st.exchange(use(contractEntry.contractID, "id"));
        st.exchange(use(contractorID, "contractor"));
        st.exchange(use(customerID, "customer"));
        st.exchange(use(escrowID, "escrow"));
        st.exchange(use(disputerID, disputeIndicator, "disputer"));
        st.exchange(use(contractEntry.startTime, "s_t"));
        st.exchange(use(contractEntry.endTime, "e_t"));
        st.exchange(use(strDetails, "details"));
        st.exchange(use(disputeReason, disputeIndicator, "dis_reason"));
        st.exchange(use(state, "state"));
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
        string contractorID, customerID, escrowID, disputerID, details, disputeReason;
        indicator disputeIndicator = i_null;

        LedgerEntry le;
        le.data.type(LedgerEntryType::CONTRACT);
        ContractEntry& oe = le.data.contract();
        int32_t state = 0;
        int32_t version = 0;

        auto& st = prep.statement();
        st.exchange(into(oe.contractID));
        st.exchange(into(contractorID));
        st.exchange(into(customerID));
        st.exchange(into(escrowID));
        st.exchange(into(disputerID, disputeIndicator));
        st.exchange(into(oe.startTime));
        st.exchange(into(oe.endTime));
        st.exchange(into(details));
        st.exchange(into(disputeReason, disputeIndicator));
        st.exchange(into(state));
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
            oe.stateInfo.state(static_cast<ContractState>(state));

            if ((oe.stateInfo.state() == ContractState::DISPUTING) && (disputeIndicator == i_ok))
            {
                oe.stateInfo.disputeDetails().disputer = PubKeyUtils::fromStrKey(disputerID);
                bn::decode_b64(disputeReason, oe.stateInfo.disputeDetails().reason);
            }

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