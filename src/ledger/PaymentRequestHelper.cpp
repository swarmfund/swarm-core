// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "PaymentRequestHelper.h"
#include "LedgerDelta.h"
#include "xdrpp/printer.h"

using namespace soci;
using namespace std;

namespace stellar {
    using xdr::operator<;

    static const char* paymentRequestColumnSelector =
            "SELECT payment_id, source_balance, source_send, source_send_universal, dest_balance, dest_receive, "
                    "created_at, invoice_id, lastmodified, version "
                    "FROM payment_request";

    void PaymentRequestHelper::dropAll(Database &db) {
        db.getSession() << "DROP TABLE IF EXISTS payment_request;";
        db.getSession() << "CREATE TABLE payment_request"
                "("
                "payment_id             BIGINT       NOT NULL CHECK (payment_id >= 0),"
                "source_balance         VARCHAR(64)  NOT NULL,"
                "source_send            BIGINT       NOT NULL CHECK (source_send >= 0),"
                "source_send_universal  BIGINT       NOT NULL CHECK (source_send_universal >= 0),"
                "dest_balance           VARCHAR(64)  NOT NULL,"
                "dest_receive           BIGINT       NOT NULL CHECK (dest_receive >= 0),"
                "created_at             BIGINT       NOT NULL,"
                "invoice_id             BIGINT       NOT NULL check(invoice_id >= 0),"
                "lastmodified           INT          NOT NULL,"
                "version                INT          NOT NULL DEFAULT 0,"
                "PRIMARY KEY (payment_id)"
                ");";;
    }

    void PaymentRequestHelper::storeAdd(LedgerDelta &delta, Database &db, LedgerEntry const &entry) {
        storeUpdateHelper(delta, db, true, entry);
    }

    void PaymentRequestHelper::storeChange(LedgerDelta &delta, Database &db, LedgerEntry const &entry) {
        storeUpdateHelper(delta, db, false, entry);
    }

    void PaymentRequestHelper::storeDelete(LedgerDelta &delta, Database &db, LedgerKey const &key) {
        auto timer = db.getDeleteTimer("payment_request");
        auto prep = db.getPreparedStatement("DELETE FROM payment_request WHERE payment_id=:s");
        auto& st = prep.statement();
        int64 paymentID = key.paymentRequest().paymentID;
        st.exchange(use(paymentID));
        st.define_and_bind();
        st.execute(true);
        delta.deleteEntry(key);
    }

    bool PaymentRequestHelper::exists(Database &db, LedgerKey const &key) {
        return exists(db, key.paymentRequest().paymentID);
    }

    LedgerKey PaymentRequestHelper::getLedgerKey(LedgerEntry const &from) {
        LedgerKey ledgerKey;
        ledgerKey.type(from.data.type());
        ledgerKey.paymentRequest().paymentID = from.data.paymentRequest().paymentID;
        return ledgerKey;
    }

    EntryFrame::pointer PaymentRequestHelper::storeLoad(LedgerKey const &key, Database &db) {
        return loadPaymentRequest(key.paymentRequest().paymentID, db);
    }

    EntryFrame::pointer PaymentRequestHelper::fromXDR(LedgerEntry const &from) {
        return std::make_shared<PaymentRequestFrame>(from);
    }

    uint64_t PaymentRequestHelper::countObjects(soci::session &sess) {
        uint64_t count = 0;
        sess << "SELECT COUNT(*) FROM payment_request;", into(count);
        return count;
    }

    bool PaymentRequestHelper::exists(Database &db, int64 paymentID) {
        int exists = 0;
        auto timer = db.getSelectTimer("payment_request-exists");
        auto prep =
                db.getPreparedStatement("SELECT EXISTS (SELECT NULL FROM payment_request WHERE payment_id=:id)");
        auto& st = prep.statement();
        st.exchange(use(paymentID));
        st.exchange(into(exists));
        st.define_and_bind();
        st.execute(true);

        return exists != 0;
    }

    void
    PaymentRequestHelper::storeUpdateHelper(LedgerDelta &delta, Database &db, bool insert, LedgerEntry const &entry) {

        auto paymentRequestFrame = make_shared<PaymentRequestFrame>(entry);
		auto paymentRequestEntry = paymentRequestFrame->getPaymentRequest();

        paymentRequestFrame->touch(delta);

        if (!paymentRequestFrame->isValid())
        {
            CLOG(ERROR, Logging::ENTRY_LOGGER)
                    << "Unexpected state - payment request is invalid: "
                    << xdr::xdr_to_string(paymentRequestEntry);
            throw std::runtime_error("Unexpected state - payment request is invalid");
        }

        std::string sourceBalance = BalanceKeyUtils::toStrKey(paymentRequestFrame->getSourceBalance());
        std::string destBalance = paymentRequestFrame->getDestinationBalance() ?
                                  BalanceKeyUtils::toStrKey(*paymentRequestFrame->getDestinationBalance()) : "";
        uint64_t invoiceID = paymentRequestFrame->getInvoiceID() ? *paymentRequestFrame->getInvoiceID() : 0;
        auto paymentRequestVersion = static_cast<int32_t >(paymentRequestFrame->getPaymentRequest().ext.v());

        string sql;

        if (insert)
        {
            sql = "INSERT INTO payment_request (payment_id, source_balance, source_send, source_send_universal, "
                    "dest_balance, dest_receive, created_at, invoice_id, lastmodified, version) "
                    "VALUES (:id, :sb, :ss, :ssu, :db, :ds, :ca, :iid, :lm, :v)";
        }
        else
        {
            return;
        }

        auto prep = db.getPreparedStatement(sql);
        auto& st = prep.statement();

        st.exchange(use(paymentRequestEntry.paymentID, "id"));
        st.exchange(use(sourceBalance, "sb"));
        st.exchange(use(paymentRequestEntry.sourceSend, "ss"));
        st.exchange(use(paymentRequestEntry.sourceSendUniversal, "ssu"));
        st.exchange(use(destBalance, "db"));
        st.exchange(use(paymentRequestEntry.destinationReceive, "ds"));
        st.exchange(use(paymentRequestEntry.createdAt, "ca"));
        st.exchange(use(invoiceID, "iid"));
        st.exchange(use(paymentRequestFrame->mEntry.lastModifiedLedgerSeq, "lm"));
        st.exchange(use(paymentRequestVersion, "v"));
        st.define_and_bind();

        auto timer = db.getInsertTimer("payment_request");
        st.execute(true);

        if (st.get_affected_rows() != 1)
        {
            throw std::runtime_error("could not update SQL");
        }

        delta.addEntry(*paymentRequestFrame);
    }

    void PaymentRequestHelper::loadPaymentRequests(StatementContext &prep,
                                                   std::function<void(LedgerEntry const &)> PaymentRequestProcessor) {
        string sourceBalance, destBalance;

        LedgerEntry le;
        le.data.type(LedgerEntryType::PAYMENT_REQUEST);
        PaymentRequestEntry& oe = le.data.paymentRequest();

        uint64 invoiceID = 0;
        int paymentRequestVersion;

        statement& st = prep.statement();
        st.exchange(into(oe.paymentID));
        st.exchange(into(sourceBalance));
        st.exchange(into(oe.sourceSend));
        st.exchange(into(oe.sourceSendUniversal));
        st.exchange(into(destBalance));
        st.exchange(into(oe.destinationReceive));
        st.exchange(into(oe.createdAt));
        st.exchange(into(invoiceID));
        st.exchange(into(le.lastModifiedLedgerSeq));
        st.exchange(into(paymentRequestVersion));
        st.define_and_bind();
        st.execute(true);
        while (st.got_data())
        {
            oe.sourceBalance = BalanceKeyUtils::fromStrKey(sourceBalance);
            oe.ext.v(LedgerVersion(paymentRequestVersion));
            if (destBalance.size() > 0)
            {
                auto destBalanceID = BalanceKeyUtils::fromStrKey(destBalance);
                oe.destinationBalance.activate() = destBalanceID;
            }

            if (invoiceID != 0)
                oe.invoiceID.activate() = invoiceID;

            if (!PaymentRequestFrame::isValid(oe))
            {
                CLOG(ERROR, Logging::ENTRY_LOGGER)
                        << "Unexpected state - payment request is invalid: "
                        << xdr::xdr_to_string(oe);
                throw std::runtime_error("Unexpected state - payment request is invalid");
            }

            PaymentRequestProcessor(le);
            st.fetch();
        }
    }

    PaymentRequestFrame::pointer
    PaymentRequestHelper::loadPaymentRequest(int64 paymentID, Database &db, LedgerDelta *delta) {
        PaymentRequestFrame::pointer retPaymentRequest;
        std::string sql = paymentRequestColumnSelector;
        sql += " WHERE payment_id = :payment_id";
        auto prep = db.getPreparedStatement(sql);
        auto& st = prep.statement();
        st.exchange(use(paymentID));

        auto timer = db.getSelectTimer("payment_request");
        loadPaymentRequests(prep, [&retPaymentRequest](LedgerEntry const& PaymentRequest)
        {
            retPaymentRequest = make_shared<PaymentRequestFrame>(PaymentRequest);
        });

        if (delta && retPaymentRequest)
        {
            delta->recordEntry(*retPaymentRequest);
        }

        return retPaymentRequest;
    }
}
