// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ledger/CoinsEmissionRequestFrame.h"
#include "database/Database.h"
#include "crypto/SecretKey.h"
#include "crypto/SHA.h"
#include "LedgerDelta.h"
#include "util/types.h"

using namespace std;
using namespace soci;

namespace stellar
{
const char* CoinsEmissionRequestFrame::kSQLCreateStatement1 =
    "CREATE TABLE coins_emission_request"
    "("
	"request_id  BIGINT NOT NULL CHECK (request_id >= 0),"
    "reference       VARCHAR(64) NOT NULL,"
    "receiver       VARCHAR(64) NOT NULL,"
	"issuer       VARCHAR(64) NOT NULL,"
    "amount       BIGINT      NOT NULL CHECK (amount >= 0),"
    "asset       VARCHAR(16) NOT NULL,"
    "is_approved  BOOLEAN     NOT NULL,"
    "lastmodified INT         NOT NULL,"
    "PRIMARY KEY (request_id), UNIQUE (reference, issuer)"
    ");";

const char* CoinsEmissionRequestFrame::kSQLCreateStatement2 =
"CREATE INDEX coins_emission_request_issuer ON coins_emission_request (issuer);";

static const char* coinsEmissionRequestColumnSelector =
"SELECT request_id, reference, receiver, issuer, amount, asset, is_approved, lastmodified "
"FROM coins_emission_request";

CoinsEmissionRequestFrame::CoinsEmissionRequestFrame() : EntryFrame(COINS_EMISSION_REQUEST), mCoinsEmissionRequest(mEntry.data.coinsEmissionRequest())
{
}

CoinsEmissionRequestFrame::CoinsEmissionRequestFrame(LedgerEntry const& from)
    : EntryFrame(from), mCoinsEmissionRequest(mEntry.data.coinsEmissionRequest())
{
}

CoinsEmissionRequestFrame::CoinsEmissionRequestFrame(CoinsEmissionRequestFrame const& from) : CoinsEmissionRequestFrame(from.mEntry)
{
}

CoinsEmissionRequestFrame& CoinsEmissionRequestFrame::operator=(CoinsEmissionRequestFrame const& other)
{
    if (&other != this)
    {
        mCoinsEmissionRequest = other.mCoinsEmissionRequest;
        mKey = other.mKey;
        mKeyCalculated = other.mKeyCalculated;
    }
    return *this;
}

bool
CoinsEmissionRequestFrame::isValid(CoinsEmissionRequestEntry const& oe)
{
    return true;
}

bool
CoinsEmissionRequestFrame::isValid() const
{
    return isValid(mCoinsEmissionRequest);
}

CoinsEmissionRequestFrame::pointer
CoinsEmissionRequestFrame::loadCoinsEmissionRequest(uint64 requestID, Database& db,
                      LedgerDelta* delta)
{
    CoinsEmissionRequestFrame::pointer retCoinsEmissionRequest;

    std::string sql = coinsEmissionRequestColumnSelector;
    sql += " WHERE request_id = :request_id";
    auto prep = db.getPreparedStatement(sql);
    auto& st = prep.statement();
    st.exchange(use(requestID));

    auto timer = db.getSelectTimer("coins_emission_request");
    loadCoinsEmissionRequests(prep, [&retCoinsEmissionRequest](LedgerEntry const& coinsEmissionRequest)
               {
                   retCoinsEmissionRequest = make_shared<CoinsEmissionRequestFrame>(coinsEmissionRequest);
               });

    if (delta && retCoinsEmissionRequest)
    {
        delta->recordEntry(*retCoinsEmissionRequest);
    }

    return retCoinsEmissionRequest;
}

void
CoinsEmissionRequestFrame::loadCoinsEmissionRequests(StatementContext& prep,
                       std::function<void(LedgerEntry const&)> coinsEmissionRequestProcessor)
{
    string issuer, requestID, receiver, asset, reference;
	int isApproved = 0;

    LedgerEntry le;
    le.data.type(COINS_EMISSION_REQUEST);
    CoinsEmissionRequestEntry& oe = le.data.coinsEmissionRequest();

    statement& st = prep.statement();
	st.exchange(into(oe.requestID));
    st.exchange(into(reference));
    st.exchange(into(receiver));
    st.exchange(into(issuer));
    st.exchange(into(oe.amount));
    st.exchange(into(asset));
	st.exchange(into(isApproved));
    st.exchange(into(le.lastModifiedLedgerSeq));
    st.define_and_bind();
    st.execute(true);
    while (st.got_data())
    {
        oe.reference = reference;
		oe.issuer = PubKeyUtils::fromStrKey(issuer);
        oe.receiver = BalanceKeyUtils::fromStrKey(receiver);
        oe.asset = asset;
		oe.isApproved = isApproved != 0;

        if (!isValid(oe))
        {
            throw std::runtime_error("Invalid coins emission request");
        }

        coinsEmissionRequestProcessor(le);
        st.fetch();
    }
}

void
CoinsEmissionRequestFrame::loadCoinsEmissionRequests(AccountID const& accountID,
                       std::vector<CoinsEmissionRequestFrame::pointer>& retCoinsEmissionRequests,
                       Database& db)
{
    std::string actIDStrKey;
    actIDStrKey = PubKeyUtils::toStrKey(accountID);

    std::string sql = coinsEmissionRequestColumnSelector;
    sql += " WHERE issuer = :id";
    auto prep = db.getPreparedStatement(sql);
    auto& st = prep.statement();
    st.exchange(use(actIDStrKey));

    auto timer = db.getSelectTimer("coins_emission_request");
    loadCoinsEmissionRequests(prep, [&retCoinsEmissionRequests](LedgerEntry const& of)
               {
                   retCoinsEmissionRequests.emplace_back(make_shared<CoinsEmissionRequestFrame>(of));
               });
}

std::unordered_map<AccountID, std::vector<CoinsEmissionRequestFrame::pointer>>
CoinsEmissionRequestFrame::loadAllCoinsEmissionRequests(Database& db)
{
    std::unordered_map<AccountID, std::vector<CoinsEmissionRequestFrame::pointer>> retCoinsEmissionRequests;
    std::string sql = coinsEmissionRequestColumnSelector;
    sql += " ORDER BY issuer";
    auto prep = db.getPreparedStatement(sql);

    auto timer = db.getSelectTimer("coins_emission_request");
    loadCoinsEmissionRequests(prep, [&retCoinsEmissionRequests](LedgerEntry const& of)
               {
                   auto& thisUserCoinsEmissionRequests = retCoinsEmissionRequests[of.data.coinsEmissionRequest().issuer];
                   thisUserCoinsEmissionRequests.emplace_back(make_shared<CoinsEmissionRequestFrame>(of));
               });
    return retCoinsEmissionRequests;
}

CoinsEmissionRequestFrame::pointer CoinsEmissionRequestFrame::create(int64_t amount, AssetCode asset,
        std::string reference, uint64_t requestID, AccountID issuer,
        AccountID receiver)
{
    LedgerEntry le;
    le.data.type(COINS_EMISSION_REQUEST);
    CoinsEmissionRequestEntry& entry = le.data.coinsEmissionRequest();
    entry.amount = amount;
    entry.asset = asset;
    entry.reference = reference;
    entry.requestID = requestID;
    entry.isApproved = false;
    entry.issuer = issuer;
    entry.receiver = receiver;
    return std::make_shared<CoinsEmissionRequestFrame>(le);
}


uint64_t
CoinsEmissionRequestFrame::countObjects(soci::session& sess)
{
    uint64_t count = 0;
    sess << "SELECT COUNT(*) FROM coins_emission_request;", into(count);
    return count;
}

void
CoinsEmissionRequestFrame::storeDelete(LedgerDelta& delta, Database& db) const
{
    storeDelete(delta, db, getKey());
}

void
CoinsEmissionRequestFrame::storeDelete(LedgerDelta& delta, Database& db, LedgerKey const& key)
{
    auto timer = db.getDeleteTimer("coins_emission_request");
    auto prep = db.getPreparedStatement("DELETE FROM coins_emission_request WHERE request_id=:s");
    auto& st = prep.statement();
    uint64 requestID = key.coinsEmissionRequest().requestID;
    st.exchange(use(requestID));
    st.define_and_bind();
    st.execute(true);
    delta.deleteEntry(key);
}

void
CoinsEmissionRequestFrame::deleteForIssuer(LedgerDelta& delta, Database& db, AccountID issuer)
{
    std::vector<CoinsEmissionRequestFrame::pointer> coinsEmissionRequests;
    CoinsEmissionRequestFrame::loadCoinsEmissionRequests(issuer, coinsEmissionRequests, db);

    auto timer = db.getDeleteTimer("coins_emission_request");
    auto prep = db.getPreparedStatement("DELETE FROM coins_emission_request WHERE issuer=:s");
    auto& st = prep.statement();
    std::string issuerStrKey = PubKeyUtils::toStrKey(issuer);
    st.exchange(use(issuerStrKey));
    st.define_and_bind();
    st.execute(true);

    for (auto it = coinsEmissionRequests.begin(); it != coinsEmissionRequests.end(); it++)
    {
        CoinsEmissionRequestFrame request = *it->get();
        delta.deleteEntry(request.getKey());
    }
}


void
CoinsEmissionRequestFrame::storeChange(LedgerDelta& delta, Database& db)
{
    storeUpdateHelper(delta, db, false);
}

void
CoinsEmissionRequestFrame::storeAdd(LedgerDelta& delta, Database& db)
{
    storeUpdateHelper(delta, db, true);
}

void
CoinsEmissionRequestFrame::storeUpdateHelper(LedgerDelta& delta, Database& db, bool insert)
{
    touch(delta);

    if (!isValid())
    {
        throw std::runtime_error("Invalid coins emission request");
    }

    std::string reference = mCoinsEmissionRequest.reference;
    std::string issuer = PubKeyUtils::toStrKey(mCoinsEmissionRequest.issuer);
    std::string receiver = BalanceKeyUtils::toStrKey(mCoinsEmissionRequest.receiver);
    std::string asset = mCoinsEmissionRequest.asset;
	int isApproved = mCoinsEmissionRequest.isApproved ? 1 : 0;

    string sql;

    if (insert)
    {
		sql = "INSERT INTO coins_emission_request (request_id, reference, receiver,"
			"issuer, amount, asset, is_approved, lastmodified)"
			"VALUES (:r_id, :ref, :rec, :is, :am, :as, :ap, :lm)";
    }
    else
    {
        sql = "UPDATE coins_emission_request SET reference=:ref, "
              "amount=:am, asset=:as, receiver=:rec, is_approved=:ap, lastmodified=:lm "
              "WHERE request_id = :r_id";
    }

    auto prep = db.getPreparedStatement(sql);
    auto& st = prep.statement();

    if (insert)
    {
        st.exchange(use(issuer, "is"));
    }

    st.exchange(use(reference, "ref"));
    st.exchange(use(receiver, "rec"));
    st.exchange(use(mCoinsEmissionRequest.requestID, "r_id"));
    st.exchange(use(mCoinsEmissionRequest.amount, "am"));
    st.exchange(use(asset, "as"));
    st.exchange(use(isApproved, "ap"));
    st.exchange(use(getLastModified(), "lm"));
    st.define_and_bind();

    auto timer =
        insert ? db.getInsertTimer("coins_emission_request") : db.getUpdateTimer("coins_emission_request");
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
CoinsEmissionRequestFrame::exists(Database& db, LedgerKey const& key)
{
	std::string issuer = PubKeyUtils::toStrKey(key.coinsEmissionRequest().issuer);
	int exists = 0;
	auto timer = db.getSelectTimer("coins_emission_request-exists");
	auto prep =
		db.getPreparedStatement("SELECT EXISTS (SELECT NULL FROM coins_emission_request WHERE issuer=:is AND request_id=:id)");
	auto& st = prep.statement();
	st.exchange(use(issuer));
    uint64 requestID = key.coinsEmissionRequest().requestID;
	st.exchange(use(requestID));
	st.exchange(into(exists));
	st.define_and_bind();
	st.execute(true);

	return exists != 0;
}

bool
CoinsEmissionRequestFrame::exists(Database& db, uint64 requestID)
{
	int exists = 0;
	auto timer = db.getSelectTimer("coins_emission_request-exists");
	auto prep =
		db.getPreparedStatement("SELECT EXISTS (SELECT NULL FROM coins_emission_request WHERE request_id=:id)");
	auto& st = prep.statement();
	st.exchange(use(requestID));
	st.exchange(into(exists));
	st.define_and_bind();
	st.execute(true);

	return exists != 0;
}

bool
CoinsEmissionRequestFrame::exists(Database& db, AccountID issuer, string reference)
{
    std::string rawIssuer = PubKeyUtils::toStrKey(issuer);

	int exists = 0;
	auto timer = db.getSelectTimer("coins_emission_request-exists");
	auto prep =
		db.getPreparedStatement("SELECT EXISTS (SELECT NULL FROM coins_emission_request WHERE issuer=:iss AND reference=:ref)");
	auto& st = prep.statement();
	st.exchange(use(rawIssuer, "iss"));
    st.exchange(use(reference, "ref"));
	st.exchange(into(exists));
	st.define_and_bind();
	st.execute(true);

	return exists != 0;
}


void
CoinsEmissionRequestFrame::dropAll(Database& db)
{
    db.getSession() << "DROP TABLE IF EXISTS coins_emission_request;";
    db.getSession() << kSQLCreateStatement1;
    db.getSession() << kSQLCreateStatement2;
}
}
