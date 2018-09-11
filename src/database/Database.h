#pragma once

// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "database/Marshaler.h"
#include "medida/timer_context.h"
#include "overlay/StellarXDR.h"
#include "util/NonCopyable.h"
#include "util/Timer.h"
#include "util/lrucache.hpp"
#include <set>
#include <soci.h>
#include <string>

namespace medida
{
class Meter;
class Timer;
class Counter;
}

namespace stellar
{
class Application;
class SQLLogContext;

/**
 * Helper class for borrowing a SOCI prepared statement handle into a local
 * scope and cleaning it up once done with it. Returned by
 * Database::getPreparedStatement below.
 */
class StatementContext : NonCopyable
{
    std::shared_ptr<soci::statement> mStmt;

  public:
    StatementContext(std::shared_ptr<soci::statement> stmt) : mStmt(stmt)
    {
        mStmt->clean_up(false);
    }
    StatementContext(StatementContext&& other)
    {
        mStmt = other.mStmt;
        other.mStmt.reset();
    }
    ~StatementContext()
    {
        if (mStmt)
        {
            mStmt->clean_up(false);
        }
    }
    soci::statement&
    statement()
    {
        return *mStmt;
    }
};

/**
 * Object that owns the database connection(s) that an application
 * uses to store the current ledger and other persistent state in.
 *
 * This may represent an in-memory SQLite instance (for testing), an on-disk
 * SQLite instance (for running a minimal, self-contained server) or a
 * connection to a local Postgresql database, that the node operator must have
 * set up on their own.
 *
 * Database connects, on construction, to the target specified by the
 * application Config object's Config::DATABASE value; this originates from the
 * config-file's DATABASE string. The default is "sqlite3://:memory:". This
 * "main connection" is where most SQL statements -- and all write-statements --
 * are executed.
 *
 * Database may establish additional connections for worker threads to read
 * data, from a separate connection pool, if worker threads request them. The
 * pool will connect to the same target and only one connection will be made per
 * worker thread.
 *
 * All database connections and transactions are set to snapshot isolation level
 * (SQL isolation level 'SERIALIZABLE' in Postgresql and Sqlite, neither of
 * which provide true serializability).
 */
class Database
{
  public:
    // Return a crude meter of total queries to the db, for use in
    // overlay/LoadManager.
    virtual medida::Meter& getQueryMeter() = 0;

    // Number of nanoseconds spent processing queries since app startup,
    // without any reference to excluded time or running counters.
    // Strictly a sum of measured time.
    virtual std::chrono::nanoseconds totalQueryTime() const = 0;

    // Subtract a number of nanoseconds from the running time counts,
    // due to database usage spikes, specifically during ledger-close.
    virtual void excludeTime(std::chrono::nanoseconds const& queryTime,
                             std::chrono::nanoseconds const& totalTime) = 0;

    // Return the percent of the time since the last call to this
    // method that database has been idle, _excluding_ the times
    // excluded above via `excludeTime`.
    virtual uint32_t recentIdleDbPercent() = 0;

    // Return a logging helper that will capture all SQL statements made
    // on the main connection while active, and will log those statements
    // to the process' log for diagnostics. For testing and perf tuning.
    virtual std::shared_ptr<SQLLogContext>
    captureAndLogSQL(std::string contextName) = 0;

    // Return a helper object that borrows, from the Database, a prepared
    // statement handle for the provided query. The prepared statement handle
    // is ceated if necessary before borrowing, and reset (unbound from data)
    // when the statement context is destroyed.
    virtual StatementContext getPreparedStatement(std::string const& query) = 0;

    // Purge all cached prepared statements, closing their handles with the
    // database.
    virtual void clearPreparedStatementCache() = 0;

    // Return metric-gathering timers for various families of SQL operation.
    // These timers automatically count the time they are alive for,
    // so only acquire them immediately before executing an SQL statement.
    virtual medida::TimerContext
    getInsertTimer(std::string const& entityName) = 0;
    virtual medida::TimerContext
    getSelectTimer(std::string const& entityName) = 0;
    virtual medida::TimerContext
    getDeleteTimer(std::string const& entityName) = 0;
    virtual medida::TimerContext
    getUpdateTimer(std::string const& entityName) = 0;

    // If possible (i.e. "on postgres") issue an SQL pragma that marks
    // the current transaction as read-only. The effects of this last
    // only as long as the current SQL transaction.
    virtual void setCurrentTransactionReadOnly() = 0;

    // Return true if the Database target is SQLite, otherwise false.
    virtual bool isSqlite() const = 0;

    // Return true if a connection pool is available for worker threads
    // to read from the database through, otherwise false.
    virtual bool canUsePool() const = 0;

    // Drop and recreate all tables in the database target. This is called
    // by the --newdb command-line flag on stellar-core.
    virtual void initialize() = 0;

    // Save `vers` as schema version.
    virtual void putSchemaVersion(unsigned long vers) = 0;

    // Get current schema version in DB.
    virtual unsigned long getDBSchemaVersion() = 0;

    // Get current schema version of running application.
    virtual unsigned long getAppSchemaVersion() = 0;

    // Check schema version and apply any upgrades if necessary.
    virtual void upgradeToCurrentSchema() = 0;

    // Access the underlying SOCI session object
    virtual soci::session& getSession() = 0;

    // Access the optional SOCI connection pool available for worker
    // threads. Throws an error if !canUsePool().
    virtual soci::connection_pool& getPool() = 0;

    // Access the LedgerEntry cache. Note: clients are responsible for
    // invalidating entries in this cache as they perform statements
    // against the database. It's kept here only for ease of access.
    typedef cache::lru_cache<std::string, std::shared_ptr<LedgerEntry const>>
        EntryCache;
    virtual EntryCache& getEntryCache() = 0;

    virtual ~Database()
    {
    }
};

class DatabaseImpl : public Database, public NonMovableOrCopyable
{
    Application& mApp;
    medida::Meter& mQueryMeter;
    soci::session mSession;
    std::unique_ptr<soci::connection_pool> mPool;

    std::map<std::string, std::shared_ptr<soci::statement>> mStatements;
    medida::Counter& mStatementsSize;

    cache::lru_cache<std::string, std::shared_ptr<LedgerEntry const>>
        mEntryCache;

    // Helpers for maintaining the total query time and calculating
    // idle percentage.
    std::set<std::string> mEntityTypes;
    std::chrono::nanoseconds mExcludedQueryTime;
    std::chrono::nanoseconds mExcludedTotalTime;
    std::chrono::nanoseconds mLastIdleQueryTime;
    VirtualClock::time_point mLastIdleTotalTime;

    static bool gDriversRegistered;
    static void registerDrivers();
    void applySchemaUpgrade(unsigned long vers);

  public:
    // Instantiate object and connect to app.getConfig().DATABASE;
    // if there is a connection error, this will throw.
    DatabaseImpl(Application& app);

  private:
    virtual medida::Meter& getQueryMeter();

    virtual std::chrono::nanoseconds totalQueryTime() const;

    virtual void excludeTime(std::chrono::nanoseconds const& queryTime,
                             std::chrono::nanoseconds const& totalTime);

    virtual uint32_t recentIdleDbPercent();

    virtual std::shared_ptr<SQLLogContext>
    captureAndLogSQL(std::string contextName);

    virtual StatementContext getPreparedStatement(std::string const& query);

    virtual void clearPreparedStatementCache();

    virtual medida::TimerContext getInsertTimer(std::string const& entityName);
    virtual medida::TimerContext getSelectTimer(std::string const& entityName);
    virtual medida::TimerContext getDeleteTimer(std::string const& entityName);
    virtual medida::TimerContext getUpdateTimer(std::string const& entityName);

    virtual void setCurrentTransactionReadOnly();

    virtual bool isSqlite() const;

    virtual bool canUsePool() const;

    virtual void initialize();

    virtual void putSchemaVersion(unsigned long vers);

    virtual unsigned long getDBSchemaVersion();

    virtual unsigned long getAppSchemaVersion();

    virtual void upgradeToCurrentSchema();

    virtual soci::session& getSession();

    virtual soci::connection_pool& getPool();

    virtual EntryCache& getEntryCache();
};

class DBTimeExcluder : NonCopyable
{
    Application& mApp;
    std::chrono::nanoseconds mStartQueryTime;
    VirtualClock::time_point mStartTotalTime;

  public:
    DBTimeExcluder(Application& mApp);
    ~DBTimeExcluder();
};
}
