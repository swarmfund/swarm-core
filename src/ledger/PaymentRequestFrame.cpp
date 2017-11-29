// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ledger/PaymentRequestFrame.h"
#include "database/Database.h"
#include "crypto/SecretKey.h"
#include "crypto/SHA.h"
#include "LedgerDelta.h"
#include "util/types.h"

using namespace std;
using namespace soci;

namespace stellar
{
const char* PaymentRequestFrame::kSQLCreateStatement1 =
    "CREATE TABLE payment_request"
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
    ");";

static const char* PaymentRequestColumnSelector =
"SELECT payment_id, source_balance, source_send, source_send_universal, dest_balance, dest_receive, "
       "created_at, invoice_id, lastmodified, version "
"FROM payment_request";

PaymentRequestFrame::PaymentRequestFrame() : EntryFrame(LedgerEntryType::PAYMENT_REQUEST), mPaymentRequest(mEntry.data.paymentRequest())
{
}

PaymentRequestFrame::PaymentRequestFrame(LedgerEntry const& from)
    : EntryFrame(from), mPaymentRequest(mEntry.data.paymentRequest())
{
}

PaymentRequestFrame::PaymentRequestFrame(PaymentRequestFrame const& from) : PaymentRequestFrame(from.mEntry)
{
}

PaymentRequestFrame& PaymentRequestFrame::operator=(PaymentRequestFrame const& other)
{
    if (&other != this)
    {
        mPaymentRequest = other.mPaymentRequest;
        mKey = other.mKey;
        mKeyCalculated = other.mKeyCalculated;
    }
    return *this;
}

bool
PaymentRequestFrame::isValid(PaymentRequestEntry const& oe)
{
	return oe.sourceSend >= 0 && oe.sourceSendUniversal >= 0 && oe.destinationReceive >= 0;
}

bool
PaymentRequestFrame::isValid() const
{
    return isValid(mPaymentRequest);
}

PaymentRequestFrame::pointer
PaymentRequestFrame::loadPaymentRequest(int64 paymentID, Database& db,
                      LedgerDelta* delta)
{
    PaymentRequestFrame::pointer retPaymentRequest;
    std::string sql = PaymentRequestColumnSelector;
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

void
PaymentRequestFrame::loadPaymentRequests(StatementContext& prep,
                       std::function<void(LedgerEntry const&)> PaymentRequestProcessor)
{
    string sourceBalance, destBalance;

    LedgerEntry le;
    le.data.type(LedgerEntryType::PAYMENT_REQUEST);
    PaymentRequestEntry& oe = le.data.paymentRequest();

	uint64 invoiceID = 0;
    int paymentRequestVersion;

	// SELECT payment_id, source_balance, source_send, source_send_universal, dest_balance, dest_receive, created_at, invoice_id, lastmodified

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

        if (!isValid(oe))
        {
            throw std::runtime_error("Invalid payment request");
        }

        PaymentRequestProcessor(le);
        st.fetch();
    }
}


    uint64_t
PaymentRequestFrame::countObjects(soci::session& sess)
{
    uint64_t count = 0;
    sess << "SELECT COUNT(*) FROM payment_request;", into(count);
    return count;
}

void
PaymentRequestFrame::storeDelete(LedgerDelta& delta, Database& db) const
{
    storeDelete(delta, db, getKey());
}

void
PaymentRequestFrame::storeDelete(LedgerDelta& delta, Database& db, LedgerKey const& key)
{
    auto timer = db.getDeleteTimer("payment_request");
    auto prep = db.getPreparedStatement("DELETE FROM payment_request WHERE payment_id=:s");
    auto& st = prep.statement();
    int64 paymentID = key.paymentRequest().paymentID;
    st.exchange(use(paymentID));
    st.define_and_bind();
    st.execute(true);
    delta.deleteEntry(key);
}

void
PaymentRequestFrame::storeChange(LedgerDelta& delta, Database& db)
{
    storeUpdateHelper(delta, db, false);
}

void
PaymentRequestFrame::storeAdd(LedgerDelta& delta, Database& db)
{
    storeUpdateHelper(delta, db, true);
}

void
PaymentRequestFrame::storeUpdateHelper(LedgerDelta& delta, Database& db, bool insert)
{
    touch(delta);

    if (!isValid())
    {
        throw std::runtime_error("Invalid payment request");
    }

    std::string sourceBalance = BalanceKeyUtils::toStrKey(mPaymentRequest.sourceBalance);
	std::string destBalance = mPaymentRequest.destinationBalance ? BalanceKeyUtils::toStrKey(*mPaymentRequest.destinationBalance) : "";
	uint64_t invoiceID = mPaymentRequest.invoiceID ? *mPaymentRequest.invoiceID : 0;
    int paymentRequestVersion = static_cast<int32_t >(mPaymentRequest.ext.v());

    string sql;

    if (insert)
    {
		sql = "INSERT INTO payment_request (payment_id, source_balance, source_send, source_send_universal, "
              "dest_balance, dest_receive, created_at, invoice_id, lastmodified, version) "
              "VALUES (:id, :sb, :ss, :ssu, :db, :ds, :ca, :iid, :lm, :v)";
    }
    else
    {
        //not supported
        return;
    }

    auto prep = db.getPreparedStatement(sql);
    auto& st = prep.statement();

    st.exchange(use(mPaymentRequest.paymentID, "id"));
    st.exchange(use(sourceBalance, "sb"));
    st.exchange(use(mPaymentRequest.sourceSend, "ss"));
    st.exchange(use(mPaymentRequest.sourceSendUniversal, "ssu"));
    st.exchange(use(destBalance, "db"));
    st.exchange(use(mPaymentRequest.destinationReceive, "ds"));
    st.exchange(use(mPaymentRequest.createdAt, "ca"));
	st.exchange(use(invoiceID, "iid"));
    st.exchange(use(getLastModified(), "lm"));
    st.exchange(use(paymentRequestVersion, "v"));
    st.define_and_bind();

    auto timer =
        insert ? db.getInsertTimer("payment_request") : db.getUpdateTimer("payment_request");
    st.execute(true);

    if (st.get_affected_rows() != 1)
    {
        throw std::runtime_error("could not update SQL");
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

bool
PaymentRequestFrame::exists(Database& db, LedgerKey const& key)
{
	int exists = 0;
	auto timer = db.getSelectTimer("forfeit_request-exists");
	auto prep =
		db.getPreparedStatement("SELECT EXISTS (SELECT NULL FROM payment_request WHERE payment_id=:id)");
	auto& st = prep.statement();
    int64 paymentID = key.paymentRequest().paymentID;
	st.exchange(use(paymentID));
	st.exchange(into(exists));
	st.define_and_bind();
	st.execute(true);

	return exists != 0;
}

bool
PaymentRequestFrame::exists(Database& db, int64 paymentID)
{
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
PaymentRequestFrame::dropAll(Database& db)
{
    db.getSession() << "DROP TABLE IF EXISTS payment_request;";
    db.getSession() << kSQLCreateStatement1;
}
}
