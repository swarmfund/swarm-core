// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ledger/InvoiceFrame.h"
#include "database/Database.h"
#include "crypto/SecretKey.h"
#include "crypto/SHA.h"
#include "LedgerDelta.h"
#include "util/types.h"

using namespace std;
using namespace soci;

namespace stellar
{
const char* InvoiceFrame::kSQLCreateStatement1 =
    "CREATE TABLE invoice"
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

static const char* invoiceColumnSelector =
"SELECT invoice_id, sender, receiver_account, receiver_balance, amount, state, lastmodified, version "
"FROM   invoice";

InvoiceFrame::InvoiceFrame() : EntryFrame(LedgerEntryType::INVOICE), mInvoice(mEntry.data.invoice())
{
}

InvoiceFrame::InvoiceFrame(LedgerEntry const& from)
    : EntryFrame(from), mInvoice(mEntry.data.invoice())
{
}

InvoiceFrame::InvoiceFrame(InvoiceFrame const& from) : InvoiceFrame(from.mEntry)
{
}

InvoiceFrame& InvoiceFrame::operator=(InvoiceFrame const& other)
{
    if (&other != this)
    {
        mInvoice = other.mInvoice;
        mKey = other.mKey;
        mKeyCalculated = other.mKeyCalculated;
    }
    return *this;
}

bool
InvoiceFrame::isValid(InvoiceEntry const& oe)
{
	return oe.amount > 0;
}

bool
InvoiceFrame::isValid() const
{
    return isValid(mInvoice);
}

InvoiceFrame::pointer
InvoiceFrame::loadInvoice(int64 invoiceID, Database& db, LedgerDelta* delta)
{
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

void
InvoiceFrame::loadInvoices(StatementContext& prep,
                       std::function<void(LedgerEntry const&)> InvoiceProcessor)
{
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
        if (!isValid(oe))
        {
            throw std::runtime_error("Invalid payment request");
        }

        InvoiceProcessor(le);
        st.fetch();
    }
}

void
InvoiceFrame::loadInvoices(AccountID const& accountID,
                       std::vector<InvoiceFrame::pointer>& retInvoices,
                       Database& db)
{
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

int64
InvoiceFrame::countForReceiverAccount(Database& db, AccountID account)
{
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



uint64_t
InvoiceFrame::countObjects(soci::session& sess)
{
    uint64_t count = 0;
    sess << "SELECT COUNT(*) FROM invoice;", into(count);
    return count;
}

void
InvoiceFrame::storeDelete(LedgerDelta& delta, Database& db) const
{
    storeDelete(delta, db, getKey());
}

void
InvoiceFrame::storeDelete(LedgerDelta& delta, Database& db, LedgerKey const& key)
{
    auto timer = db.getDeleteTimer("invoice");
    auto prep = db.getPreparedStatement("DELETE FROM invoice WHERE invoice_id=:id");
    auto& st = prep.statement();
    int64 invoiceID = key.invoice().invoiceID;
    st.exchange(use(invoiceID));
    st.define_and_bind();
    st.execute(true);
    delta.deleteEntry(key);
}

void
InvoiceFrame::storeChange(LedgerDelta& delta, Database& db)
{
    storeUpdateHelper(delta, db, false);
}

void
InvoiceFrame::storeAdd(LedgerDelta& delta, Database& db)
{
    storeUpdateHelper(delta, db, true);
}

void
InvoiceFrame::storeUpdateHelper(LedgerDelta& delta, Database& db, bool insert)
{
    touch(delta);

    if (!isValid())
    {
        throw std::runtime_error("Invalid invoice");
    }

    std::string sender = PubKeyUtils::toStrKey(mInvoice.sender);
    std::string receiverAccount = PubKeyUtils::toStrKey(mInvoice.receiverAccount);
    std::string receiverBalance = BalanceKeyUtils::toStrKey(mInvoice.receiverBalance);
    int32_t invoiceVersion = static_cast<int32_t >(mInvoice.ext.v());

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
	int32 state = static_cast<int32_t >(mInvoice.state);

    st.exchange(use(mInvoice.invoiceID, "id"));
    st.exchange(use(sender, "s"));
    st.exchange(use(receiverAccount, "ra"));
    st.exchange(use(receiverBalance, "rb"));
    st.exchange(use(mInvoice.amount, "am"));
    st.exchange(use(state, "st"));
    st.exchange(use(getLastModified(), "lm"));
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
        delta.addEntry(*this);
    }
    else
    {
        delta.modEntry(*this);
    }
}

bool
InvoiceFrame::exists(Database& db, LedgerKey const& key)
{
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

void
InvoiceFrame::dropAll(Database& db)
{
    db.getSession() << "DROP TABLE IF EXISTS invoice;";
    db.getSession() << kSQLCreateStatement1;
}
}
