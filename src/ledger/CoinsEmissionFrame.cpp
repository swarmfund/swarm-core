// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ledger/CoinsEmissionFrame.h"
#include "database/Database.h"
#include "crypto/SecretKey.h"
#include "crypto/SHA.h"
#include "LedgerDelta.h"
#include "util/types.h"

using namespace std;
using namespace soci;

namespace stellar
{
const char* CoinsEmissionFrame::kSQLCreateStatement1 =
    "CREATE TABLE coins_emission"
    "("
    "serial_number VARCHAR(64) NOT NULL UNIQUE,"
    "amount       BIGINT      NOT NULL CHECK (amount >= 0),"
    "asset VARCHAR(16) NOT NULL,"
	"lastmodified    INT      NOT NULL,"
    "PRIMARY KEY (serial_number)"
    ");";

static const char* coinsEmissionColumnSelector =
"SELECT serial_number, amount, asset, lastmodified "
"FROM coins_emission";

CoinsEmissionFrame::CoinsEmissionFrame() : EntryFrame(COINS_EMISSION), mCoinsEmission(mEntry.data.coinsEmission())
{
}

CoinsEmissionFrame::CoinsEmissionFrame(LedgerEntry const& from)
    : EntryFrame(from), mCoinsEmission(mEntry.data.coinsEmission())
{
}

CoinsEmissionFrame::CoinsEmissionFrame(CoinsEmissionFrame const& from) : CoinsEmissionFrame(from.mEntry)
{
}

CoinsEmissionFrame& CoinsEmissionFrame::operator=(CoinsEmissionFrame const& other)
{
    if (&other != this)
    {
        mCoinsEmission = other.mCoinsEmission;
        mKey = other.mKey;
        mKeyCalculated = other.mKeyCalculated;
    }
    return *this;
}

bool
CoinsEmissionFrame::isValid(CoinsEmissionEntry const& oe)
{
    return true;
}

bool
CoinsEmissionFrame::isValid() const
{
    return isValid(mCoinsEmission);
}

CoinsEmissionFrame::pointer
CoinsEmissionFrame::loadCoinsEmission(string serialNumber, Database& db,
                      LedgerDelta* delta)
{
    CoinsEmissionFrame::pointer retCoinsEmission;

    std::string sql = coinsEmissionColumnSelector;
    sql += " where serial_number = :sn";
    auto prep = db.getPreparedStatement(sql);
    auto& st = prep.statement();
    st.exchange(use(serialNumber));

    auto timer = db.getSelectTimer("coins_emission");
    loadCoinsEmissions(prep, [&retCoinsEmission](LedgerEntry const& CoinsEmission)
               {
                   retCoinsEmission = make_shared<CoinsEmissionFrame>(CoinsEmission);
               });

    if (delta && retCoinsEmission)
    {
        delta->recordEntry(*retCoinsEmission);
    }

    return retCoinsEmission;
}

void
CoinsEmissionFrame::loadCoinsEmissions(StatementContext& prep,
                       std::function<void(LedgerEntry const&)> coinsEmissionProcessor)
{
    string serialNumber, asset;

    LedgerEntry le;
    le.data.type(COINS_EMISSION);
    CoinsEmissionEntry& oe = le.data.coinsEmission();


    statement& st = prep.statement();
	st.exchange(into(serialNumber));
    st.exchange(into(oe.amount));
    st.exchange(into(asset));
	st.exchange(into(le.lastModifiedLedgerSeq));
    st.define_and_bind();
    st.execute(true);
    while (st.got_data())
    {
        oe.serialNumber = serialNumber;
        oe.asset = asset;

        if (!isValid(oe))
        {
            throw std::runtime_error("Invalid coins emission");
        }

        coinsEmissionProcessor(le);
        st.fetch();
    }
}


uint64_t
CoinsEmissionFrame::countObjects(soci::session& sess)
{
    uint64_t count = 0;
    sess << "SELECT COUNT(*) FROM coins_emission;", into(count);
    return count;
}

void
CoinsEmissionFrame::storeDelete(LedgerDelta& delta, Database& db) const
{
    storeDelete(delta, db, getKey());
}

void
CoinsEmissionFrame::storeDelete(LedgerDelta& delta, Database& db, LedgerKey const& key)
{
    auto timer = db.getDeleteTimer("coins_emission");
    auto prep = db.getPreparedStatement("DELETE FROM coins_emission WHERE serial_number=:sn");
    auto& st = prep.statement();
    string serialNumber = key.coinsEmission().serialNumber;
    st.exchange(use(serialNumber));
    st.define_and_bind();
    st.execute(true);
    delta.deleteEntry(key);
}

void
CoinsEmissionFrame::storeChange(LedgerDelta& delta, Database& db)
{
    storeUpdateHelper(delta, db, false);
}

void
CoinsEmissionFrame::storeAdd(LedgerDelta& delta, Database& db)
{
    storeUpdateHelper(delta, db, true);
}

void
CoinsEmissionFrame::storePreEmissions(vector<shared_ptr<CoinsEmissionFrame>> preEmissions, LedgerDelta& delta, Database& db)
{
    string sql = "INSERT INTO coins_emission ("
			"serial_number, amount, asset, lastmodified)"
			"VALUES (:sn, :am, :as, :lm)";
    auto prep = db.getPreparedStatement(sql);
    auto& st = prep.statement();
    vector<std::string> serials(preEmissions.size());
    vector<std::string> assets(preEmissions.size());
    vector<uint64> amounts(preEmissions.size());
    vector<uint64> lastModifieds(preEmissions.size());
    for (int i = 0; i < preEmissions.size(); i++)
    {
        auto currentCoinsEmissionFrame = preEmissions[i];
        currentCoinsEmissionFrame->touch(delta);

        serials[i] = currentCoinsEmissionFrame->getSerialNumber();
        assets[i] = currentCoinsEmissionFrame->getAsset();
        amounts[i] = currentCoinsEmissionFrame->getAmount();
        lastModifieds[i] = currentCoinsEmissionFrame->getLastModified();

        delta.addEntry(*currentCoinsEmissionFrame);
    }
    st.exchange(use(serials, "sn"));
    st.exchange(use(assets, "as"));
    st.exchange(use(amounts, "am"));
    st.exchange(use(lastModifieds, "lm"));
    st.define_and_bind();

    auto timer = db.getInsertTimer("coins_emission");
    st.execute(true);

    if (st.get_affected_rows() != int64_t(preEmissions.size()))
    {
        throw std::runtime_error("could not update SQL");
    }
    return;
}

void
CoinsEmissionFrame::storeUpdateHelper(LedgerDelta& delta, Database& db, bool insert)
{
    touch(delta);

    if (!isValid())
    {
        throw std::runtime_error("Invalid coins emission");
    }

    std::string serialNumber = mCoinsEmission.serialNumber;
    std::string asset = mCoinsEmission.asset;

    string sql;

    if (insert)
    {
		sql = "INSERT INTO coins_emission ("
			"serial_number, amount, asset, lastmodified)"
			"VALUES (:sn, :am, :as, :lm)";
    }
    else
    {
        sql = "UPDATE coins_emission SET "
			"amount=:am, asset = :as, lastmodified=:lm "
              "WHERE serial_number = :sn";
    }

    auto prep = db.getPreparedStatement(sql);
    auto& st = prep.statement();


    st.exchange(use(serialNumber, "sn"));
    st.exchange(use(mCoinsEmission.amount, "am"));
    st.exchange(use(asset, "as"));
	st.exchange(use(mEntry.lastModifiedLedgerSeq, "lm"));
    st.define_and_bind();

    auto timer =
        insert ? db.getInsertTimer("coins_emission") : db.getUpdateTimer("coins_emission");
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
CoinsEmissionFrame::exists(Database& db, LedgerKey const& key)
{
	int exists = 0;
	auto timer = db.getSelectTimer("coins_emission-exists");
	auto prep =
		db.getPreparedStatement("SELECT EXISTS (SELECT NULL FROM coins_emission WHERE serial_number=:sn)");
	auto& st = prep.statement();
    string serialNumber = key.coinsEmission().serialNumber;
	st.exchange(use(serialNumber));
	st.exchange(into(exists));
	st.define_and_bind();
	st.execute(true);

	return exists != 0;
}

bool
CoinsEmissionFrame::exists(Database& db, std::string serialNumber)
{
	int exists = 0;
	auto timer = db.getSelectTimer("coins_emission-exists");
	auto prep =
		db.getPreparedStatement("SELECT EXISTS (SELECT NULL FROM coins_emission WHERE serial_number=:sn)");
	auto& st = prep.statement();
	st.exchange(use(serialNumber));
	st.exchange(into(exists));
	st.define_and_bind();
	st.execute(true);

	return exists != 0;
}


bool
CoinsEmissionFrame::exists(Database& db, vector<std::string> serialNumbers)
{
    if (serialNumbers.size() == 0)
        return false;
	int count = 0;
	auto timer = db.getSelectTimer("coins_emission-exists");
    string query = "SELECT COUNT(*) FROM coins_emission "
			"WHERE serial_number IN (:sn0";
    char serialNumberKey[10];
    for (int i = 1; i < serialNumbers.size(); i++)
    {
        snprintf(serialNumberKey, sizeof(serialNumberKey), ", :sn%i", i);

        query = query + serialNumberKey;
    }
    query = query + ")";
    auto prep = db.getPreparedStatement(query);
    auto& st = prep.statement();
    for (int i = 0; i < serialNumbers.size(); i++)
    {
        snprintf(serialNumberKey, sizeof(serialNumberKey), "sn%i", i);
        st.exchange(use(serialNumbers[i], serialNumberKey));
    }
    st.exchange(into(count));
    st.define_and_bind();
    st.execute(true);

	return count != 0;
}

void
CoinsEmissionFrame::dropAll(Database& db)
{
    db.getSession() << "DROP TABLE IF EXISTS coins_emission;";
    db.getSession() << kSQLCreateStatement1;
}
}
