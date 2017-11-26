// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ledger/ReferenceFrame.h"
#include "database/Database.h"
#include "crypto/SecretKey.h"
#include "crypto/SHA.h"
#include "LedgerDelta.h"
#include "util/types.h"
#include "xdrpp/printer.h"

using namespace std;
using namespace soci;

namespace stellar
{

ReferenceFrame::ReferenceFrame() : EntryFrame(REFERENCE_ENTRY), mReference(mEntry.data.reference())
{
}

ReferenceFrame::ReferenceFrame(LedgerEntry const& from)
    : EntryFrame(from), mReference(mEntry.data.reference())
{
}

ReferenceFrame::ReferenceFrame(ReferenceFrame const& from) : ReferenceFrame(from.mEntry)
{
}

ReferenceFrame& ReferenceFrame::operator=(ReferenceFrame const& other)
{
    if (&other != this)
    {
        mReference = other.mReference;
        mKey = other.mKey;
        mKeyCalculated = other.mKeyCalculated;
    }
    return *this;
}

ReferenceFrame::pointer ReferenceFrame::create(AccountID sender, stellar::string64 reference)
{
	LedgerEntry le;
	le.data.type(LedgerEntryType::REFERENCE_ENTRY);
	auto& referenceEntry = le.data.reference();
	referenceEntry.reference = reference;
	referenceEntry.sender = sender;
	return std::make_shared<ReferenceFrame>(le);
}

bool
ReferenceFrame::isValid(ReferenceEntry const& oe)
{
    return true;
}

bool
ReferenceFrame::isValid() const
{
    return isValid(mReference);
}

ReferenceFrame::pointer
ReferenceFrame::loadReference(AccountID sender, string reference, Database& db,
                      LedgerDelta* delta)
{
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

void
ReferenceFrame::loadReferences(StatementContext& prep,
                       std::function<void(LedgerEntry const&)> referenceProcessor)
{
    LedgerEntry le;
    le.data.type(REFERENCE_ENTRY);
    ReferenceEntry& oe = le.data.reference();


    statement& st = prep.statement();
    st.exchange(into(oe.sender));
    st.exchange(into(oe.reference));
    st.exchange(into(le.lastModifiedLedgerSeq));
    st.define_and_bind();
    st.execute(true);
    while (st.got_data())
    {
        if (!isValid(oe))
        {
			CLOG(ERROR, Logging::ENTRY_LOGGER) << "Invalid reference: " << xdr::xdr_to_string(oe);
			throw std::runtime_error("Invalid reference");
        }

        referenceProcessor(le);
        st.fetch();
    }
}

uint64_t
ReferenceFrame::countObjects(soci::session& sess)
{
    uint64_t count = 0;
    sess << "SELECT COUNT(*) FROM reference;", into(count);
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
    auto timer = db.getDeleteTimer("reference");
    auto prep = db.getPreparedStatement("DELETE FROM reference WHERE reference=:r AND sender=:se");
    auto& st = prep.statement();
    st.exchange(use(key.reference().reference));
    st.exchange(use(key.reference().sender));
    st.define_and_bind();
    st.execute(true);
    delta.deleteEntry(key);
}

void
ReferenceFrame::storeChange(LedgerDelta& delta, Database& db)
{
	throw std::runtime_error("Update for reference is not supported");
}

void
ReferenceFrame::storeAdd(LedgerDelta& delta, Database& db)
{
	touch(delta);

	if (!isValid())
	{
		CLOG(ERROR, Logging::ENTRY_LOGGER) << "Invalid reference: " << xdr::xdr_to_string(mEntry);
		throw std::runtime_error("Invalid reference");
	}

	string sql = "INSERT INTO reference (reference, sender, lastmodified) VALUES (:r, :se, :lm)";

	auto prep = db.getPreparedStatement(sql);
	auto& st = prep.statement();

	st.exchange(use(mReference.reference, "r"));
	st.exchange(use(mReference.sender, "se"));
	st.exchange(use(getLastModified(), "lm"));
	st.define_and_bind();

	auto timer = db.getInsertTimer("reference");
	st.execute(true);
	if (st.get_affected_rows() != 1)
	{
		throw std::runtime_error("could not update SQL");
	}

	delta.addEntry(*this);
}

bool
ReferenceFrame::exists(Database& db, LedgerKey const& key)
{
	return exists(db, key.reference().reference, key.reference().sender);
}

bool
ReferenceFrame::exists(Database& db, string reference, AccountID sender)
{
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

void
ReferenceFrame::dropAll(Database& db)
{
    db.getSession() << "DROP TABLE IF EXISTS reference;";
    db.getSession() << "CREATE TABLE reference"
    "("
	"sender       VARCHAR(64) NOT NULL,"
    "reference    VARCHAR(64) NOT NULL,"
    "lastmodified INT         NOT NULL,"
    "PRIMARY KEY (sender, reference)"
		");";
}
}
