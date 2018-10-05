#pragma once

#include "database/Database.h"
#include "medida/meter.h"

namespace stellar
{

class MockDatabase : public Database
{
  public:
    MOCK_METHOD0(getQueryMeter, medida::Meter&());
    MOCK_CONST_METHOD0(totalQueryTime, std::chrono::nanoseconds());
    MOCK_METHOD2(excludeTime, void(std::chrono::nanoseconds const& queryTime,
                                   std::chrono::nanoseconds const& totalTime));
    MOCK_METHOD0(recentIdleDbPercent, uint32_t());
    MOCK_METHOD1(captureAndLogSQL,
                 std::shared_ptr<SQLLogContext>(std::string contextName));
    MOCK_METHOD1(getPreparedStatement,
                 StatementContext(std::string const& query));
    MOCK_METHOD0(clearPreparedStatementCache, void());
    MOCK_METHOD1(getInsertTimer,
                 medida::TimerContext(std::string const& entityName));
    MOCK_METHOD1(getSelectTimer,
                 medida::TimerContext(std::string const& entityName));
    MOCK_METHOD1(getDeleteTimer,
                 medida::TimerContext(std::string const& entityName));
    MOCK_METHOD1(getUpdateTimer,
                 medida::TimerContext(std::string const& entityName));
    MOCK_METHOD0(setCurrentTransactionReadOnly, void());
    MOCK_CONST_METHOD0(isSqlite, bool());
    MOCK_CONST_METHOD0(canUsePool, bool());
    MOCK_METHOD0(initialize, void());
    MOCK_METHOD1(putSchemaVersion, void(unsigned long vers));
    MOCK_METHOD0(getDBSchemaVersion, unsigned long());
    MOCK_METHOD0(getAppSchemaVersion, unsigned long());
    MOCK_METHOD0(upgradeToCurrentSchema, void());
    MOCK_METHOD0(getSession, soci::session&());
    MOCK_METHOD0(getPool, soci::connection_pool&());
    MOCK_METHOD0(getEntryCache, Database::EntryCache&());
};

} // namespace stellar