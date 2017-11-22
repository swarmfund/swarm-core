#pragma once

// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ledger/EntryFrame.h"
#include <functional>
#include <unordered_map>

namespace soci
{
class session;
}

namespace stellar
{
class StatementContext;

class StatisticsFrame : public EntryFrame
{
    static void
    loadStatistics(StatementContext& prep,
               std::function<void(LedgerEntry const&)> assetProcessor);

    StatisticsEntry& mStatistics;

    StatisticsFrame(StatisticsFrame const& from);

    void storeUpdateHelper(LedgerDelta& delta, Database& db, bool insert);

  public:
    typedef std::shared_ptr<StatisticsFrame> pointer;

    StatisticsFrame();
    StatisticsFrame(LedgerEntry const& from);

    StatisticsFrame& operator=(StatisticsFrame const& other);

    EntryFrame::pointer
    copy() const override
    {
        return EntryFrame::pointer(new StatisticsFrame(*this));
    }

    StatisticsEntry const&
    getStatistics() const
    {
        return mStatistics;
    }
    StatisticsEntry&
    getStatistics()
    {
        return mStatistics;
    }

	void clearObsolete(time_t rawCurrentTime, bool useImprovedVersion);
	bool add(int64 outcome, time_t currentTime, time_t timePerformed, bool useImprovedVersion);

    static bool isValid(StatisticsEntry const& oe);
    bool isValid() const;

    // Instance-based overrides of EntryFrame.
    void storeDelete(LedgerDelta& delta, Database& db) const override;
    void storeChange(LedgerDelta& delta, Database& db) override;
    void storeAdd(LedgerDelta& delta, Database& db) override;

    // Static helpers that don't assume an instance.
    static void storeDelete(LedgerDelta& delta, Database& db,
                            LedgerKey const& key);
    static bool exists(Database& db, LedgerKey const& key);
    static uint64_t countObjects(soci::session& sess);

    // database utilities
	static pointer loadStatistics(AccountID const& accountID, Database& db, LedgerDelta* delta = nullptr);
	static pointer mustLoadStatistics(AccountID const& accountID, Database& db, LedgerDelta* delta = nullptr)
	{
		auto result = loadStatistics(accountID, db, delta);
		if (!result)
		{
			CLOG(ERROR, Logging::ENTRY_LOGGER) << "Unexpected db state. Expected statistics to exists. AccountID " << PubKeyUtils::toStrKey(accountID);
			throw std::runtime_error("Unexpected db state. Expected statistics to exist");
		}

		return result;
	}

    static void dropAll(Database& db);
    static const char* kSQLCreateStatement1;
};
}
