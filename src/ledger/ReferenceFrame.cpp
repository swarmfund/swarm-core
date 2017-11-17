// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ledger/ReferenceFrame.h"
#include "database/Database.h"
#include "crypto/SecretKey.h"
#include "crypto/SHA.h"
#include "LedgerDelta.h"
#include "util/types.h"

using namespace std;
using namespace soci;

namespace stellar
{
const char* ReferenceFrame::kSQLCreateStatement1 =
    "CREATE TABLE payment"
    "("
	"sender       VARCHAR(64) NOT NULL,"
    "reference       VARCHAR(64) NOT NULL,"
    "lastmodified INT         NOT NULL,"
    "PRIMARY KEY (sender, reference)"
    ");";

const char* ReferenceFrame::kSQLCreateStatement2 =
"CREATE INDEX payment_sender ON payment (sender);";

static const char* PaymentColumnSelector =
"SELECT sender, reference, lastmodified "
"FROM payment";

ReferenceFrame::ReferenceFrame() : EntryFrame(REFERENCE_ENTRY), mPayment(mEntry.data.payment())
{
}

ReferenceFrame::ReferenceFrame(LedgerEntry const& from)
    : EntryFrame(from), mPayment(mEntry.data.payment())
{
}

ReferenceFrame::ReferenceFrame(ReferenceFrame const& from) : ReferenceFrame(from.mEntry)
{
}

ReferenceFrame& ReferenceFrame::operator=(ReferenceFrame const& other)
{
    if (&other != this)
    {
        mPayment = other.mPayment;
        mKey = other.mKey;
        mKeyCalculated = other.mKeyCalculated;
    }
    return *this;
}

bool
ReferenceFrame::isValid(ReferenceEntry const& oe)
{
    return true;
}

bool
ReferenceFrame::isValid() const
{
    return isValid(mPayment);
}

ReferenceFrame::pointer
ReferenceFrame::loadPayment(AccountID sender, string reference, Database& db,
                      LedgerDelta* delta)
{
    ReferenceFrame::pointer retPayment;
    auto senderIDStrKey = PubKeyUtils::toStrKey(sender);
    std::string sql = PaymentColumnSelector;
    sql += " WHERE reference = :reference AND sender = :sender";
    auto prep = db.getPreparedStatement(sql);
    auto& st = prep.statement();
    st.exchange(use(reference));
    st.exchange(use(senderIDStrKey));

    auto timer = db.getSelectTimer("payment");
    loadPayments(prep, [&retPayment](LedgerEntry const& Payment)
               {
                   retPayment = make_shared<ReferenceFrame>(Payment);
               });

    if (delta && retPayment)
    {
        delta->recordEntry(*retPayment);
    }

    return retPayment;
}

void
ReferenceFrame::loadPayments(StatementContext& prep,
                       std::function<void(LedgerEntry const&)> PaymentProcessor)
{
    string sender, reference;

    LedgerEntry le;
    le.data.type(REFERENCE_ENTRY);
    ReferenceEntry& oe = le.data.payment();


    statement& st = prep.statement();
    st.exchange(into(sender));
    st.exchange(into(reference));
    st.exchange(into(le.lastModifiedLedgerSeq));
    st.define_and_bind();
    st.execute(true);
    while (st.got_data())
    {
		oe.sender = PubKeyUtils::fromStrKey(sender);
        oe.reference = reference;

        if (!isValid(oe))
        {
            throw std::runtime_error("Invalid payment");
        }

        PaymentProcessor(le);
        st.fetch();
    }
}

uint64_t
ReferenceFrame::countObjects(soci::session& sess)
{
    uint64_t count = 0;
    sess << "SELECT COUNT(*) FROM payment;", into(count);
    return count;
}

void
ReferenceFrame::storeDelete(LedgerDelta& delta, Database& db) const
{
    storeDelete(delta, db, getKey());
}

void
ReferenceFrame::storeDelete(LedgerDelta& delta, Database& db, LedgerKey const& key)
{
	std::string sender = PubKeyUtils::toStrKey(key.payment().sender);
    std::string reference = key.payment().reference;
    auto timer = db.getDeleteTimer("payment");
    auto prep = db.getPreparedStatement("DELETE FROM payment WHERE reference=:r AND sender=:se");
    auto& st = prep.statement();
    st.exchange(use(reference));
    st.exchange(use(sender));
    st.define_and_bind();
    st.execute(true);
    delta.deleteEntry(key);
}

void
ReferenceFrame::storeChange(LedgerDelta& delta, Database& db)
{
    storeUpdateHelper(delta, db, false);
}

void
ReferenceFrame::storeAdd(LedgerDelta& delta, Database& db)
{
    storeUpdateHelper(delta, db, true);
}

void
ReferenceFrame::storeUpdateHelper(LedgerDelta& delta, Database& db, bool insert)
{
    touch(delta);

    if (!isValid())
    {
        throw std::runtime_error("Invalid payment");
    }

    std::string sender = PubKeyUtils::toStrKey(mPayment.sender);
    std::string reference = mPayment.reference;

    string sql;

    if (insert)
    {
		sql = "INSERT INTO payment (reference,"
			"sender, lastmodified)"
			"VALUES (:r, :se, :lm)";
    }
    else
    {
        //not supported
        return;
    }

    auto prep = db.getPreparedStatement(sql);
    auto& st = prep.statement();

    st.exchange(use(sender, "se"));

    st.exchange(use(reference, "r"));
    st.exchange(use(getLastModified(), "lm"));
    st.define_and_bind();

    auto timer =
        insert ? db.getInsertTimer("payment") : db.getUpdateTimer("payment");
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
ReferenceFrame::exists(Database& db, LedgerKey const& key)
{
	std::string sender = PubKeyUtils::toStrKey(key.payment().sender);
    std::string reference = key.payment().reference;
	int exists = 0;
	auto timer = db.getSelectTimer("payment-exists");
	auto prep =
		db.getPreparedStatement("SELECT EXISTS (SELECT NULL FROM payment WHERE reference=:r AND sender=:se)");
	auto& st = prep.statement();
	st.exchange(use(reference));
    st.exchange(use(sender));
	st.exchange(into(exists));
	st.define_and_bind();
	st.execute(true);

	return exists != 0;
}

bool
ReferenceFrame::exists(Database& db, string reference, AccountID sender)
{
    std::string seIDStrKey = PubKeyUtils::toStrKey(sender);
	int exists = 0;
	auto timer = db.getSelectTimer("payment-exists");
	auto prep =
		db.getPreparedStatement("SELECT EXISTS (SELECT NULL FROM payment WHERE reference=:r AND sender=:se)");
	auto& st = prep.statement();
	st.exchange(use(reference));
    st.exchange(use(seIDStrKey));
	st.exchange(into(exists));
	st.define_and_bind();
	st.execute(true);

	return exists != 0;
}

void
ReferenceFrame::dropAll(Database& db)
{
    db.getSession() << "DROP TABLE IF EXISTS payment;";
    db.getSession() << kSQLCreateStatement1;
    db.getSession() << kSQLCreateStatement2;
}
}
