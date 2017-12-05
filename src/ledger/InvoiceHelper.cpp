// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "InvoiceHelper.h"
#include "LedgerDelta.h"
#include "xdrpp/printer.h"

using namespace soci;
using namespace std;

namespace stellar {
    using xdr::operator<;

    static const char* invoiceColumnSelector =
            "SELECT invoice_id, sender, receiver_account, receiver_balance, amount, state, lastmodified, version "
                    "FROM   invoice";

    void InvoiceHelper::dropAll(Database &db) {
        db.getSession() << "DROP TABLE IF EXISTS invoice;";
        db.getSession() << "CREATE TABLE invoice"
                "("
                "invoice_id         BIGINT      PRIMARY KEY,"
                "sender             VARCHAR(64) NOT NULL,"
                "receiver_account   VARCHAR(64) NOT NULL,"
                "receiver_balance   VARCHAR(64) NOT NULL,"
                "amount             BIGINT      NOT NULL CHECK (amount >= 0),"
                "state              INT         NOT NULL CHECK (state >= 0),"
                "lastmodified       INT         NOT NULL,"
                "version            INT         NOT NULL DEFAULT 0"
                ");";
    }

    void InvoiceHelper::storeAdd(LedgerDelta &delta, Database &db, const LedgerEntry &entry) {
        storeUpdateHelper(delta, db, true, entry);
    }

    void InvoiceHelper::storeChange(LedgerDelta &delta, Database &db, const LedgerEntry &entry) {
        storeUpdateHelper(delta, db, false, entry);
    }

    void InvoiceHelper::storeDelete(LedgerDelta &delta, Database &db, const LedgerKey &key) {
        auto timer = db.getDeleteTimer("invoice");
        auto prep = db.getPreparedStatement("DELETE FROM invoice WHERE invoice_id=:id");
        auto& st = prep.statement();
        int64 invoiceID = key.invoice().invoiceID;
        st.exchange(use(invoiceID));
        st.define_and_bind();
        st.execute(true);
        delta.deleteEntry(key);
    }

    bool InvoiceHelper::exists(Database &db, const LedgerKey &key) {
        int exists = 0;
        auto timer = db.getSelectTimer("invoice-exists");
        auto prep =
                db.getPreparedStatement("SELECT EXISTS (SELECT NULL FROM invoice WHERE invoice_id=:id)");
        auto& st = prep.statement();
        int64 invoiceID = key.invoice().invoiceID;
        st.exchange(use(invoiceID));
        st.exchange(into(exists));
        st.define_and_bind();
        st.execute(true);

        return exists != 0;
    }

    LedgerKey InvoiceHelper::getLedgerKey(const LedgerEntry &from) {
        LedgerKey ledgerKey;
        ledgerKey.type(from.data.type());
        ledgerKey.invoice().invoiceID = from.data.invoice().invoiceID;
        return ledgerKey;
    }

    EntryFrame::pointer InvoiceHelper::storeLoad(const LedgerKey &key, Database &db) {
        return loadInvoice(key.invoice().invoiceID, db);
    }

    EntryFrame::pointer InvoiceHelper::fromXDR(const LedgerEntry &from) {
        return make_shared<InvoiceFrame>(from);
    }

    uint64_t InvoiceHelper::countObjects(session &sess) {
        uint64_t count = 0;
        sess << "SELECT COUNT(*) FROM invoice;", into(count);
        return count;
    }

    void
    InvoiceHelper::loadInvoices(StatementContext &prep, std::function<void(LedgerEntry const &)> InvoiceProcessor) {
        string sender, receiverAccount, receiverBalance;
        int32 state;
        LedgerEntry le;
        le.data.type(LedgerEntryType::INVOICE);
        InvoiceEntry& oe = le.data.invoice();
        int32_t invoiceVersion = 0;

        statement& st = prep.statement();
        st.exchange(into(oe.invoiceID));
        st.exchange(into(sender));
        st.exchange(into(receiverAccount));
        st.exchange(into(receiverBalance));
        st.exchange(into(oe.amount));
        st.exchange(into(state));
        st.exchange(into(le.lastModifiedLedgerSeq));
        st.exchange(into(invoiceVersion));
        st.define_and_bind();
        st.execute(true);
        while (st.got_data())
        {
            oe.sender = PubKeyUtils::fromStrKey(sender);
            oe.receiverBalance = BalanceKeyUtils::fromStrKey(receiverBalance);
            oe.receiverAccount = PubKeyUtils::fromStrKey(receiverAccount);
            oe.state = InvoiceState(state);
            oe.ext.v((LedgerVersion)invoiceVersion);
            if (!InvoiceFrame::isValid(oe))
            {
                CLOG(ERROR, Logging::ENTRY_LOGGER)
                        << "Unexpected state - invoice is invalid: "
                        << xdr::xdr_to_string(oe);
                throw std::runtime_error("Unexpected state - invoice is invalid");
            }

            InvoiceProcessor(le);
            st.fetch();
        }
    }

    InvoiceFrame::pointer InvoiceHelper::loadInvoice(int64 invoiceID, Database &db, LedgerDelta *delta) {
        InvoiceFrame::pointer retInvoice;
        std::string sql = invoiceColumnSelector;
        sql += " WHERE invoice_id = :id";
        auto prep = db.getPreparedStatement(sql);
        auto& st = prep.statement();
        st.exchange(use(invoiceID));

        auto timer = db.getSelectTimer("invoice");
        loadInvoices(prep, [&retInvoice](LedgerEntry const& Invoice)
        {
            retInvoice = make_shared<InvoiceFrame>(Invoice);
        });

        if (delta && retInvoice)
        {
            delta->recordEntry(*retInvoice);
        }

        return retInvoice;
    }

    void InvoiceHelper::loadInvoices(AccountID const &accountID, std::vector<InvoiceFrame::pointer> &retInvoices,
                                     Database &db) {
        std::string actIDStrKey;
        actIDStrKey = PubKeyUtils::toStrKey(accountID);

        std::string sql = invoiceColumnSelector;
        sql += " WHERE receiver_account = :account_id";
        auto prep = db.getPreparedStatement(sql);
        auto& st = prep.statement();
        st.exchange(use(actIDStrKey));

        auto timer = db.getSelectTimer("invoice");
        loadInvoices(prep, [&retInvoices](LedgerEntry const& of)
        {
            retInvoices.emplace_back(make_shared<InvoiceFrame>(of));
        });
    }

    int64 InvoiceHelper::countForReceiverAccount(Database &db, AccountID account) {
        int total = 0;
        auto timer = db.getSelectTimer("invoice-count");
        auto prep =
                db.getPreparedStatement("SELECT COUNT(*) FROM invoice "
                                                "WHERE receiver_account=:receiver;");
        auto& st = prep.statement();

        std::string actIDStrKey;
        actIDStrKey = PubKeyUtils::toStrKey(account);

        st.exchange(use(actIDStrKey));
        st.exchange(into(total));
        st.define_and_bind();
        st.execute(true);

        return total;
    }

    void InvoiceHelper::storeUpdateHelper(LedgerDelta &delta, Database &db, bool insert, LedgerEntry const &entry) {

        auto invoiceFrame = make_shared<InvoiceFrame>(entry);
        invoiceFrame->touch(delta);

        if (!invoiceFrame->isValid())
        {
            CLOG(ERROR, Logging::ENTRY_LOGGER)
                    << "Unexpected state - invoice is invalid: "
                    << xdr::xdr_to_string(invoiceFrame->getInvoice());
            throw std::runtime_error("Unexpected invoice - offer is invalid");
        }


        std::string sender = PubKeyUtils::toStrKey(invoiceFrame->getSender());
        std::string receiverAccount = PubKeyUtils::toStrKey(invoiceFrame->getReceiverAccount());
        std::string receiverBalance = BalanceKeyUtils::toStrKey(invoiceFrame->getReceiverBalance());
        auto invoiceVersion = static_cast<int32_t >(invoiceFrame->getInvoice().ext.v());

        string sql;

        if (insert)
        {
            sql = "INSERT INTO invoice (invoice_id, sender, receiver_account, receiver_balance, amount, state, "
                    "lastmodified, version) "
                    "VALUES (:id, :s, :ra, :rb, :am, :st, :lm, :v)";
        }
        else
        {
            sql = "UPDATE invoice "
                    "SET    sender=:s, receiver_account = :ra, receiver_balance=:rb, amount=:am, state=:st, "
                    "lastmodified=:lm, version=:v "
                    "WHERE  invoice_id=:id";
        }

        auto prep = db.getPreparedStatement(sql);
        auto& st = prep.statement();
        auto state = static_cast<int32_t >(invoiceFrame->getState());

        st.exchange(use(invoiceFrame->getID(), "id"));
        st.exchange(use(sender, "s"));
        st.exchange(use(receiverAccount, "ra"));
        st.exchange(use(receiverBalance, "rb"));
        st.exchange(use(invoiceFrame->getAmount(), "am"));
        st.exchange(use(state, "st"));
        st.exchange(use(invoiceFrame->getLastModified(), "lm"));
        st.exchange(use(invoiceVersion, "v"));
        st.define_and_bind();

        auto timer =
                insert ? db.getInsertTimer("invoice") : db.getUpdateTimer("invoice");
        st.execute(true);

        if (st.get_affected_rows() != 1)
        {
            throw std::runtime_error("could not update SQL");
        }

        if (insert)
        {
            delta.addEntry(*invoiceFrame);
        }
        else
        {
            delta.modEntry(*invoiceFrame);
        }
    }

}