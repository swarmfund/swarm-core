//
// Created by kirill on 05.12.17.
//

#include "ReferenceHelper.h"
#include "LedgerDelta.h"
#include "xdrpp/printer.h"

using namespace soci;
using namespace std;

namespace stellar {
    using xdr::operator<;

    void ReferenceHelper::dropAll(Database &db) {
        db.getSession() << "DROP TABLE IF EXISTS reference;";
        db.getSession() << "CREATE TABLE reference"
                "("
                "sender       VARCHAR(64) NOT NULL,"
                "reference    VARCHAR(64) NOT NULL,"
                "lastmodified INT         NOT NULL,"
                "PRIMARY KEY (sender, reference)"
                ");";
    }

    void ReferenceHelper::storeAdd(LedgerDelta &delta, Database &db, LedgerEntry const &entry) {

        auto reference = make_shared<ReferenceFrame>(entry);

        reference->touch(delta);

        if (!reference->isValid())
        {
            CLOG(ERROR, Logging::ENTRY_LOGGER) << "Invalid reference: " << xdr::xdr_to_string(entry);
            throw std::runtime_error("Invalid reference");
        }

        string sql = "INSERT INTO reference (reference, sender, lastmodified) VALUES (:r, :se, :lm)";

        auto prep = db.getPreparedStatement(sql);
        auto& st = prep.statement();

        st.exchange(use(reference->getReferenceString(), "r"));
        st.exchange(use(reference->getSender(), "se"));
        st.exchange(use(reference->getLastModified(), "lm"));
        st.define_and_bind();

        auto timer = db.getInsertTimer("reference");
        st.execute(true);
        if (st.get_affected_rows() != 1)
        {
            throw std::runtime_error("could not update SQL");
        }

        delta.addEntry(*reference);
    }

    void ReferenceHelper::storeChange(LedgerDelta &delta, Database &db, LedgerEntry const &entry) {
        throw std::runtime_error("Update for reference is not supported");
    }

    void ReferenceHelper::storeDelete(LedgerDelta &delta, Database &db, LedgerKey const &key) {
        auto timer = db.getDeleteTimer("reference");
        auto prep = db.getPreparedStatement("DELETE FROM reference WHERE reference=:r AND sender=:se");
        auto& st = prep.statement();
        st.exchange(use(key.reference().reference));
        st.exchange(use(key.reference().sender));
        st.define_and_bind();
        st.execute(true);
        delta.deleteEntry(key);
    }

    bool ReferenceHelper::exists(Database &db, LedgerKey const &key) {
        return exists(db, key.reference().reference, key.reference().sender);
    }

    LedgerKey ReferenceHelper::getLedgerKey(LedgerEntry const &from) {
        return LedgerKey();
    }

    EntryFrame::pointer ReferenceHelper::storeLoad(LedgerKey const &key, Database &db) {
        return EntryFrame::pointer();
    }

    EntryFrame::pointer ReferenceHelper::fromXDR(LedgerEntry const &from) {
        return EntryFrame::pointer();
    }

    uint64_t ReferenceHelper::countObjects(soci::session &sess) {
        uint64_t count = 0;
        sess << "SELECT COUNT(*) FROM reference;", into(count);
        return count;
    }

    void
    ReferenceHelper::loadReferences(StatementContext &prep, function<void(const LedgerEntry &)> referenceProcessor) {
        LedgerEntry le;
        le.data.type(LedgerEntryType::REFERENCE_ENTRY);
        ReferenceEntry& oe = le.data.reference();

        statement& st = prep.statement();
        st.exchange(into(oe.sender));
        st.exchange(into(oe.reference));
        st.exchange(into(le.lastModifiedLedgerSeq));
        st.define_and_bind();
        st.execute(true);
        while (st.got_data())
        {
            if (!ReferenceFrame::isValid(oe))
            {
                CLOG(ERROR, Logging::ENTRY_LOGGER)
                        << "Unexpected state - references is invalid: "
                        << xdr::xdr_to_string(oe);
                throw std::runtime_error("Unexpected state - references is invalid");
            }

            referenceProcessor(le);
            st.fetch();
        }
    }

    bool ReferenceHelper::exists(Database &db, std::string reference, AccountID sender) {
        int exists = 0;
        auto timer = db.getSelectTimer("reference-exists");
        auto prep =
                db.getPreparedStatement("SELECT EXISTS (SELECT NULL FROM reference WHERE reference=:r AND sender=:se)");
        auto& st = prep.statement();
        st.exchange(use(reference));
        st.exchange(use(sender));
        st.exchange(into(exists));
        st.define_and_bind();
        st.execute(true);

        return exists != 0;
    }

    ReferenceFrame::pointer
    ReferenceHelper::loadReference(AccountID sender, std::string reference, Database &db, LedgerDelta *delta) {
        std::string sql = "SELECT sender, reference, lastmodified FROM reference";
        sql += " WHERE reference = :ref AND sender = :sender";
        auto prep = db.getPreparedStatement(sql);
        auto& st = prep.statement();
        st.exchange(use(reference, "ref"));
        st.exchange(use(sender, "sender"));

        auto timer = db.getSelectTimer("reference");
        ReferenceFrame::pointer retReference;
        loadReferences(prep, [&retReference](LedgerEntry const& Reference)
        {
            retReference = make_shared<ReferenceFrame>(Reference);
        });

        if (delta && retReference)
        {
            delta->recordEntry(*retReference);
        }

        return retReference;
    }
}