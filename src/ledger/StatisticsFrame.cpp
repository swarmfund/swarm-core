// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ledger/StatisticsFrame.h"
#include "database/Database.h"
#include "crypto/SecretKey.h"
#include "crypto/SHA.h"
#include "LedgerDelta.h"
#include "util/types.h"
#include <time.h>


using namespace std;
using namespace soci;

namespace stellar
{
const char* StatisticsFrame::kSQLCreateStatement1 =
    "CREATE TABLE statistics"
    "("
	"account_id       VARCHAR(56) NOT NULL,"
	"daily_out        BIGINT 	  NOT NULL,"
	"weekly_out  	  BIGINT 	  NOT NULL,"
	"monthly_out      BIGINT 	  NOT NULL,"
	"annual_out	      BIGINT 	  NOT NULL,"
	"updated_at       BIGINT 	  NOT NULL,"
    "lastmodified     INT 		  NOT NULL,"
	"version		  INT 		  NOT NULL	DEFAULT 0,"
    "PRIMARY KEY  (account_id)"
    ");";

static const char* statisticsColumnSelector =
    "SELECT account_id, daily_out, weekly_out, monthly_out, annual_out, updated_at, lastmodified, version "
	"FROM   statistics";

StatisticsFrame::StatisticsFrame() : EntryFrame(LedgerEntryType::STATISTICS), mStatistics(mEntry.data.stats())
{
}

StatisticsFrame::StatisticsFrame(LedgerEntry const& from)
    : EntryFrame(from), mStatistics(mEntry.data.stats())
{
}

StatisticsFrame::StatisticsFrame(StatisticsFrame const& from) : StatisticsFrame(from.mEntry)
{
}

StatisticsFrame& StatisticsFrame::operator=(StatisticsFrame const& other)
{
    if (&other != this)
    {
        mStatistics = other.mStatistics;
        mKey = other.mKey;
        mKeyCalculated = other.mKeyCalculated;
    }
    return *this;
}

bool
StatisticsFrame::isValid(StatisticsEntry const& se)
{
	bool res =  se.dailyOutcome >= 0;
	res = res && (se.monthlyOutcome >= se.dailyOutcome);
	res = res && (se.annualOutcome >= se.annualOutcome);
	return res;
}

bool
StatisticsFrame::isValid() const
{
    return isValid(mStatistics);
}

StatisticsFrame::pointer StatisticsFrame::loadStatistics(AccountID const& accountID,
    Database& db, LedgerDelta* delta)
{
	std::string strAccountID = PubKeyUtils::toStrKey(accountID);

	std::string sql = statisticsColumnSelector;
	sql += " WHERE account_id = :id";
	auto prep = db.getPreparedStatement(sql);
	auto& st = prep.statement();
	st.exchange(use(strAccountID));

	auto timer = db.getSelectTimer("statistics");
	pointer retStatistics;
	loadStatistics(prep, [&retStatistics](LedgerEntry const& statistics)
	{
		retStatistics = std::make_shared<StatisticsFrame>(statistics);
	});

	if (delta && retStatistics)
	{
		delta->recordEntry(*retStatistics);
	}

	return retStatistics;
}

void
StatisticsFrame::loadStatistics(StatementContext& prep,
                       std::function<void(LedgerEntry const&)> statisticsProcessor)
{
	std::string accountID;

    LedgerEntry le;
    le.data.type(LedgerEntryType::STATISTICS);
    StatisticsEntry& se = le.data.stats();
	int32_t statisticsVersion = 0;

    statement& st = prep.statement();
    st.exchange(into(accountID));

	st.exchange(into(se.dailyOutcome));
	st.exchange(into(se.weeklyOutcome));
	st.exchange(into(se.monthlyOutcome));
	st.exchange(into(se.annualOutcome));

	st.exchange(into(se.updatedAt));
    st.exchange(into(le.lastModifiedLedgerSeq));
	st.exchange(into(statisticsVersion));
    st.define_and_bind();
    st.execute(true);
    while (st.got_data())
    {
		se.accountID = PubKeyUtils::fromStrKey(accountID);
		se.ext.v((LedgerVersion)statisticsVersion);

        if (!isValid(se))
        {
            throw std::runtime_error("Invalid statistics");
        }

        statisticsProcessor(le);
        st.fetch();
    }
}


bool
StatisticsFrame::exists(Database& db, LedgerKey const& key)
{
	std::string strAccountID = PubKeyUtils::toStrKey(key.stats().accountID);
    int exists = 0;
    auto timer = db.getSelectTimer("statistics-exists");
    auto prep =
        db.getPreparedStatement("SELECT EXISTS (SELECT NULL FROM statistics WHERE account_id=:id)");
    auto& st = prep.statement();
    st.exchange(use(strAccountID));
    st.exchange(into(exists));
    st.define_and_bind();
    st.execute(true);
    return exists != 0;
}

uint64_t
StatisticsFrame::countObjects(soci::session& sess)
{
    uint64_t count = 0;
    sess << "SELECT COUNT(*) FROM statistics;", into(count);
    return count;
}

void
StatisticsFrame::storeDelete(LedgerDelta& delta, Database& db) const
{
    storeDelete(delta, db, getKey());
}

void
StatisticsFrame::storeDelete(LedgerDelta& delta, Database& db, LedgerKey const& key)
{
    return;
}

void
StatisticsFrame::storeChange(LedgerDelta& delta, Database& db)
{
    storeUpdateHelper(delta, db, false);
}

void
StatisticsFrame::storeAdd(LedgerDelta& delta, Database& db)
{
    storeUpdateHelper(delta, db, true);
}

void
StatisticsFrame::storeUpdateHelper(LedgerDelta& delta, Database& db, bool insert)
{
    touch(delta);

    if (!isValid())
    {
        throw std::runtime_error("Invalid statistics");
    }

	std::string strAccountID = PubKeyUtils::toStrKey(mStatistics.accountID);
	int32_t statisticsVersion = static_cast<int32_t >(mStatistics.ext.v());

    string sql;

    if (insert)
    {
		//  
        sql = "INSERT INTO statistics (account_id, daily_out, "
			  							"weekly_out, monthly_out, annual_out, updated_at, lastmodified, version) "
			  "VALUES "
              "(:aid, :d_out, :w_out, :m_out, :a_out, :up, :lm, :v)";
    }
    else
    {
        sql = "UPDATE statistics "
			  "SET 	  daily_out=:d_out, weekly_out=:w_out, monthly_out=:m_out, annual_out=:a_out, "
					 "updated_at=:up, lastmodified=:lm, version=:v "
			  "WHERE  account_id=:aid";
    }

    auto prep = db.getPreparedStatement(sql);
    auto& st = prep.statement();

    st.exchange(use(strAccountID, "aid"));
	st.exchange(use(mStatistics.dailyOutcome, "d_out"));
	st.exchange(use(mStatistics.weeklyOutcome, "w_out"));
	st.exchange(use(mStatistics.monthlyOutcome, "m_out"));
	st.exchange(use(mStatistics.annualOutcome, "a_out"));
	st.exchange(use(mStatistics.updatedAt, "up"));
    st.exchange(use(getLastModified(), "lm"));
	st.exchange(use(statisticsVersion, "v"));
    st.define_and_bind();

    auto timer =
        insert ? db.getInsertTimer("statistics") : db.getUpdateTimer("statistics");
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

void StatisticsFrame::clearObsolete(time_t rawCurrentTime)
{   
	struct tm currentTime = VirtualClock::tm_from_time_t(rawCurrentTime);
 
	struct tm timeUpdated = VirtualClock::tm_from_time_t(mStatistics.updatedAt);
	
    bool isYear = timeUpdated.tm_year < currentTime.tm_year;
	if (isYear) 
	{
		mStatistics.annualOutcome = 0;
	}

	bool isMonth = isYear || timeUpdated.tm_mon < currentTime.tm_mon;
	if (isMonth)
	{
		mStatistics.monthlyOutcome = 0;
	}

	bool isWeek = VirtualClock::weekPassed(timeUpdated, currentTime);
	if (isWeek)
	{
		mStatistics.weeklyOutcome = 0;
	}

	bool isDay = isYear || timeUpdated.tm_yday < currentTime.tm_yday;
	if (isDay)
	{
		mStatistics.dailyOutcome = 0;
	}
}

bool StatisticsFrame::add(int64 outcome, time_t rawCurrentTime, time_t rawTimePerformed)
{
	clearObsolete(rawCurrentTime);
	struct tm currentTime = VirtualClock::tm_from_time_t(rawCurrentTime);
	struct tm timePerformed = VirtualClock::tm_from_time_t(rawTimePerformed);
	if (currentTime.tm_year != timePerformed.tm_year)
	{
		return true;
	}
	mStatistics.annualOutcome += outcome;
	if (mStatistics.annualOutcome < 0)
	{
		return false;
	}

	if (currentTime.tm_mon != timePerformed.tm_mon)
	{
		return true;
	}
	mStatistics.monthlyOutcome += outcome;
	if (mStatistics.monthlyOutcome < 0)
	{
		return false;
	}

	bool isWeek = VirtualClock::weekPassed(timePerformed, currentTime);
	if (isWeek)
	{
		return true;
	}

	mStatistics.weeklyOutcome += outcome;
	if (mStatistics.weeklyOutcome < 0)
	{
		return false;
	}

	if (currentTime.tm_yday != timePerformed.tm_yday)
	{
		return true;
	}
	mStatistics.dailyOutcome += outcome;
    
    
    mStatistics.updatedAt = rawCurrentTime;
	return mStatistics.dailyOutcome >= 0;
}

void
StatisticsFrame::dropAll(Database& db)
{
    db.getSession() << "DROP TABLE IF EXISTS statistics;";
    db.getSession() << kSQLCreateStatement1;
}
}
