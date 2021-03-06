// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "database/Database.h"
#include "overlay/StellarXDR.h"
#include "main/Application.h"
#include "main/Config.h"
#include "util/Logging.h"
#include "util/make_unique.h"
#include "util/types.h"
#include "util/GlobalChecks.h"
#include "util/Timer.h"
#include "crypto/Hex.h"

#include "ledger/AccountFrame.h"
#include "ledger/AccountLimitsFrame.h"
#include "ledger/AccountTypeLimitsFrame.h"
#include "ledger/AssetFrame.h"
#include "ledger/BalanceFrame.h"
#include "ledger/EntryHelperLegacy.h"
#include "ledger/FeeFrame.h"
#include "ledger/FeeHelper.h"
#include "ledger/ReferenceFrame.h"
#include "ledger/StatisticsFrame.h"
#include "ledger/AssetPairFrame.h"
#include "ledger/TrustFrame.h"
#include "ledger/OfferFrame.h"
#include "ledger/ReviewableRequestFrame.h"
#include "ledger/ExternalSystemAccountID.h"
#include "ledger/AccountRoleHelper.h"
#include "ledger/ExternalSystemAccountIDPoolEntryHelperLegacy.h"
#include "ledger/StorageHelperImpl.h"
#include "overlay/OverlayManager.h"
#include "overlay/BanManager.h"
#include "main/PersistentState.h"
#include "main/ExternalQueue.h"
#include "ledger/LedgerHeaderFrame.h"
#include "transactions/TransactionFrame.h"
#include "bucket/BucketManager.h"
#include "herder/Herder.h"
#include "ledger/AccountHelper.h"
#include "medida/metrics_registry.h"
#include "medida/timer.h"
#include "medida/counter.h"

#include <stdexcept>
#include <vector>
#include <sstream>
#include <thread>
#include <ledger/AccountKYCHelper.h>
#include <ledger/AccountRolePermissionHelperImpl.h>
#include <ledger/KeyValueHelperLegacy.h>
#include <ledger/LimitsV2Helper.h>
#include <ledger/StatisticsV2Helper.h>
#include <ledger/PendingStatisticsHelper.h>
#include <ledger/ReviewableRequestHelper.h>
#include <ledger/ContractHelper.h>
#include "ledger/SaleHelper.h"
#include "ledger/ReferenceHelper.h"
#include "ledger/SaleAnteHelper.h"

extern "C" void register_factory_sqlite3();

#ifdef USE_POSTGRES
extern "C" void register_factory_postgresql();
#endif

// NOTE: soci will just crash and not throw
//  if you misname a column in a query. yay!

namespace stellar
{

using namespace soci;
using namespace std;

bool DatabaseImpl::gDriversRegistered = false;

enum databaseSchemaVersion : unsigned long {
	DROP_SCP = 2,
	INITIAL = 3,
	DROP_BAN = 4,
    REFERENCE_VERSION = 5,
    ADD_SALE_TYPE = 6,
	USE_KYC_LEVEL = 7,
    ADD_ACCOUNT_KYC = 8,
    ADD_FEE_ASSET = 9,
    EXTERNAL_POOL_FIX_DB_TYPES = 10,
    EXTERNAL_POOL_FIX_MIGRATION = 11,
    KEY_VALUE_FIX_MIGRATION = 12,
    EXTERNAL_POOL_FIX_PARENT_DB_TYPE = 13,
    ADD_SALE_ANTE = 14,
    ADD_SALE_STATE = 15,
    ADD_LIMITS_V2 = 16,
    ADD_REVIEWABLE_REQUEST_TASKS = 17,
    ADD_CONTRACTS = 18,
    REVIEWABLE_REQUEST_FIX_DEFAULT_VALUE = 19,
    REVIEWABLE_REQUEST_FIX_EXTERNAL_DETAILS = 20,
    ADD_CUSTOMER_DETAILS_TO_CONTRACT = 21,
    ADD_ACCOUNT_ROLES_AND_POLICIES = 22
};

static unsigned long const SCHEMA_VERSION = databaseSchemaVersion::ADD_ACCOUNT_ROLES_AND_POLICIES;

static void
setSerializable(soci::session& sess)
{
    sess << "SET SESSION CHARACTERISTICS AS TRANSACTION ISOLATION LEVEL "
            "SERIALIZABLE";
}

void
DatabaseImpl::registerDrivers()
{
    if (!gDriversRegistered)
    {
        register_factory_sqlite3();
#ifdef USE_POSTGRES
        register_factory_postgresql();
#endif
        gDriversRegistered = true;
    }
}

DatabaseImpl::DatabaseImpl(Application& app)
    : mApp(app)
    , mQueryMeter(
          app.getMetrics().NewMeter({"database", "query", "exec"}, "query"))
    , mStatementsSize(
          app.getMetrics().NewCounter({"database", "memory", "statements"}))
    , mEntryCache(4096)
    , mExcludedQueryTime(0)
    , mExcludedTotalTime(0)
    , mLastIdleQueryTime(0)
    , mLastIdleTotalTime(app.getClock().now())
{
    registerDrivers();
    CLOG(INFO, "Database") << "Connecting to: " << app.getConfig().DATABASE;
    mSession.open(app.getConfig().DATABASE);
    if (isSqlite())
    {
        mSession << "PRAGMA journal_mode = WAL";
        // busy_timeout gives room for external processes
        // that may lock the database for some time
        mSession << "PRAGMA busy_timeout = 10000";
    }
    else
    {
        setSerializable(mSession);
    }
}

void
DatabaseImpl::applySchemaUpgrade(unsigned long vers)
{
    clearPreparedStatementCache();

    StorageHelperImpl storageHelper(*this, nullptr);
    static_cast<StorageHelper&>(storageHelper).release();
    switch (vers) {
        case databaseSchemaVersion::DROP_SCP:
            Herder::dropAll(*this);
            break;
        case databaseSchemaVersion::INITIAL:
            break;
        case databaseSchemaVersion::DROP_BAN:
            BanManager::dropAll(*this);
            break;
        case ADD_SALE_TYPE:
            SaleHelper::Instance()->addType(*this);
            break;
        case REFERENCE_VERSION:
            ReferenceHelper::addVersion(*this);
            break;
        case databaseSchemaVersion::USE_KYC_LEVEL:
            AccountHelper::Instance()->addKYCLevel(*this);
            break;
        case databaseSchemaVersion::ADD_ACCOUNT_KYC:
            AccountKYCHelper::Instance()->dropAll(*this);
            break;
        case databaseSchemaVersion::ADD_FEE_ASSET:
            FeeHelper::Instance()->addFeeAsset(*this);
            break;
        case databaseSchemaVersion::EXTERNAL_POOL_FIX_DB_TYPES:
            break;
        case databaseSchemaVersion::EXTERNAL_POOL_FIX_MIGRATION:
            ExternalSystemAccountIDPoolEntryHelperLegacy::Instance()->dropAll(*this);
            break;
        case databaseSchemaVersion::KEY_VALUE_FIX_MIGRATION:
            KeyValueHelperLegacy::Instance()->dropAll(*this);
            break;
        case databaseSchemaVersion::EXTERNAL_POOL_FIX_PARENT_DB_TYPE:
            ExternalSystemAccountIDPoolEntryHelperLegacy::Instance()->parentToNumeric(*this);
            break;
        case databaseSchemaVersion::ADD_SALE_ANTE:
            SaleAnteHelper::Instance()->dropAll(*this);
            break;
        case databaseSchemaVersion::ADD_SALE_STATE:
            SaleHelper::Instance()->addState(*this);
            break;
        case databaseSchemaVersion::ADD_LIMITS_V2:
            LimitsV2Helper::Instance()->dropAll(*this);
            StatisticsV2Helper::Instance()->dropAll(*this);
            PendingStatisticsHelper::Instance()->dropAll(*this);
            break;
        case databaseSchemaVersion::ADD_REVIEWABLE_REQUEST_TASKS:
            ReviewableRequestHelper::Instance()->addTasks(*this);
            PendingStatisticsHelper::Instance()->restrictUpdateDelete(*this);
            break;
        case databaseSchemaVersion::ADD_CONTRACTS:
            ContractHelper::Instance()->dropAll(*this);
            break;
        case databaseSchemaVersion::REVIEWABLE_REQUEST_FIX_DEFAULT_VALUE:
            ReviewableRequestHelper::Instance()->changeDefaultExternalDetails(*this);
            break;
        case databaseSchemaVersion::REVIEWABLE_REQUEST_FIX_EXTERNAL_DETAILS:
            ReviewableRequestHelper::Instance()->setEmptyStringToExternalDetailsInsteadNull(*this);
            break;
        case databaseSchemaVersion::ADD_CUSTOMER_DETAILS_TO_CONTRACT:
            ContractHelper::Instance()->addCustomerDetails(*this);
            break;
        case databaseSchemaVersion::ADD_ACCOUNT_ROLES_AND_POLICIES:
            std::make_unique<AccountRoleHelper>(storageHelper)->dropAll();
            AccountHelper::Instance()->addAccountRole(*this);
            std::unique_ptr<AccountRolePermissionHelper>(new AccountRolePermissionHelperImpl(storageHelper))->dropAll();
            break;
        default:
            throw std::runtime_error("Unknown DB schema version");
    }
}

void
DatabaseImpl::upgradeToCurrentSchema()
{
    auto vers = getDBSchemaVersion();
    if (vers > SCHEMA_VERSION)
    {
        std::string s = ("DB schema version " + std::to_string(vers) +
                         " is newer than application schema " +
                         std::to_string(SCHEMA_VERSION));
        throw std::runtime_error(s);
    }
    while (vers < SCHEMA_VERSION)
    {
        ++vers;
        CLOG(INFO, "Database") << "Applying DB schema upgrade to version "
                               << vers;
        applySchemaUpgrade(vers);
        putSchemaVersion(vers);
    }
    assert(vers == SCHEMA_VERSION);
}

void
DatabaseImpl::putSchemaVersion(unsigned long vers)
{
    mApp.getPersistentState().setState(PersistentState::kDatabaseSchema,
                                       std::to_string(vers));
}

unsigned long
DatabaseImpl::getDBSchemaVersion()
{
    auto vstr =
        mApp.getPersistentState().getState(PersistentState::kDatabaseSchema);
    unsigned long vers = 0;
    try
    {
        vers = std::stoul(vstr);
    }
    catch (...)
    {
    }
    if (vers == 0)
    {
        throw std::runtime_error("No DB schema version found, try --newdb");
    }
    return vers;
}

unsigned long
DatabaseImpl::getAppSchemaVersion()
{
    return SCHEMA_VERSION;
}

medida::TimerContext
DatabaseImpl::getInsertTimer(std::string const& entityName)
{
    mEntityTypes.insert(entityName);
    mQueryMeter.Mark();
    return mApp.getMetrics()
        .NewTimer({"database", "insert", entityName})
        .TimeScope();
}

medida::TimerContext
DatabaseImpl::getSelectTimer(std::string const& entityName)
{
    mEntityTypes.insert(entityName);
    mQueryMeter.Mark();
    return mApp.getMetrics()
        .NewTimer({"database", "select", entityName})
        .TimeScope();
}

medida::TimerContext
DatabaseImpl::getDeleteTimer(std::string const& entityName)
{
    mEntityTypes.insert(entityName);
    mQueryMeter.Mark();
    return mApp.getMetrics()
        .NewTimer({"database", "delete", entityName})
        .TimeScope();
}

medida::TimerContext
DatabaseImpl::getUpdateTimer(std::string const& entityName)
{
    mEntityTypes.insert(entityName);
    mQueryMeter.Mark();
    return mApp.getMetrics()
        .NewTimer({"database", "update", entityName})
        .TimeScope();
}

void
DatabaseImpl::setCurrentTransactionReadOnly()
{
    if (!isSqlite())
    {
        auto prep = getPreparedStatement("SET TRANSACTION READ ONLY");
        auto& st = prep.statement();
        st.define_and_bind();
        st.execute(false);
    }
}

bool
DatabaseImpl::isSqlite() const
{
    return mApp.getConfig().DATABASE.find("sqlite3:") != std::string::npos;
}

bool
DatabaseImpl::canUsePool() const
{
    return !(mApp.getConfig().DATABASE == ("sqlite3://:memory:"));
}

void
DatabaseImpl::clearPreparedStatementCache()
{
    // Flush all prepared statements; in sqlite they represent open cursors
    // and will conflict with any DROP TABLE commands issued below
    for (auto st : mStatements)
    {
        st.second->clean_up(true);
    }
    mStatements.clear();
    mStatementsSize.set_count(mStatements.size());
}

void
DatabaseImpl::initialize()
{
    clearPreparedStatementCache();
    // normally you do not want to touch this section as
    // schema updates are done in applySchemaUpgrade

    // only time this section should be modified is when
    // consolidating changes found in applySchemaUpgrade here
	EntryHelperProvider::dropAll(*this);
    OverlayManager::dropAll(*this);
    PersistentState::dropAll(*this);
    ExternalQueue::dropAll(*this);
    LedgerHeaderFrame::dropAll(*this);
    TransactionFrame::dropAll(*this);
    HistoryManager::dropAll(*this);
    BucketManager::dropAll(mApp);
    putSchemaVersion(1);
}

soci::session&
DatabaseImpl::getSession()
{
    // global session can only be used from the main thread
    assertThreadIsMain();
    return mSession;
}

soci::connection_pool&
DatabaseImpl::getPool()
{
    if (!mPool)
    {
        std::string const& c = mApp.getConfig().DATABASE;
        if (!canUsePool())
        {
            std::string s("Can't create connection pool to ");
            s += c;
            throw std::runtime_error(s);
        }
        size_t n = std::thread::hardware_concurrency();
        LOG(INFO) << "Establishing " << n << "-entry connection pool to: " << c;
        mPool = make_unique<soci::connection_pool>(n);
        for (size_t i = 0; i < n; ++i)
        {
            LOG(DEBUG) << "Opening pool entry " << i;
            soci::session& sess = mPool->at(i);
            sess.open(c);
            if (!isSqlite())
            {
                setSerializable(sess);
            }
        }
    }
    assert(mPool);
    return *mPool;
}

cache::lru_cache<std::string, std::shared_ptr<LedgerEntry const>>&
DatabaseImpl::getEntryCache()
{
    return mEntryCache;
}

class SQLLogContext : NonCopyable
{
    std::string mName;
    soci::session& mSess;
    std::ostringstream mCapture;

  public:
    SQLLogContext(std::string const& name, soci::session& sess)
        : mName(name), mSess(sess)
    {
        mSess.set_log_stream(&mCapture);
    }
    ~SQLLogContext()
    {
        mSess.set_log_stream(nullptr);
        std::string captured = mCapture.str();
        std::istringstream rd(captured);
        std::string buf;
        CLOG(INFO, "Database") << "";
        CLOG(INFO, "Database") << "";
        CLOG(INFO, "Database") << "[SQL] -----------------------";
        CLOG(INFO, "Database") << "[SQL] begin capture: " << mName;
        CLOG(INFO, "Database") << "[SQL] -----------------------";
        while (std::getline(rd, buf))
        {
            CLOG(INFO, "Database") << "[SQL:" << mName << "] " << buf;
            buf.clear();
        }
        CLOG(INFO, "Database") << "[SQL] -----------------------";
        CLOG(INFO, "Database") << "[SQL] end capture: " << mName;
        CLOG(INFO, "Database") << "[SQL] -----------------------";
        CLOG(INFO, "Database") << "";
        CLOG(INFO, "Database") << "";
    }
};

StatementContext
DatabaseImpl::getPreparedStatement(std::string const& query)
{
    auto i = mStatements.find(query);
    std::shared_ptr<soci::statement> p;
    if (i == mStatements.end())
    {
        p = std::make_shared<soci::statement>(mSession);
        p->alloc();
        p->prepare(query);
        mStatements.insert(std::make_pair(query, p));
        mStatementsSize.set_count(mStatements.size());
    }
    else
    {
        p = i->second;
    }
    StatementContext sc(p);
    return sc;
}

std::shared_ptr<SQLLogContext>
DatabaseImpl::captureAndLogSQL(std::string contextName)
{
    return make_shared<SQLLogContext>(contextName, mSession);
}

medida::Meter&
DatabaseImpl::getQueryMeter()
{
    return mQueryMeter;
}

std::chrono::nanoseconds
DatabaseImpl::totalQueryTime() const
{
    std::vector<std::string> qtypes = {"insert", "delete", "select", "update"};
    std::chrono::nanoseconds nsq(0);
    for (auto const& q : qtypes)
    {
        for (auto const& e : mEntityTypes)
        {
            auto& timer = mApp.getMetrics().NewTimer({"database", q, e});
            uint64_t sumns = static_cast<uint64_t>(
                timer.sum() *
                static_cast<double>(timer.duration_unit().count()));
            nsq += std::chrono::nanoseconds(sumns);
        }
    }
    return nsq;
}

void
DatabaseImpl::excludeTime(std::chrono::nanoseconds const& queryTime,
                      std::chrono::nanoseconds const& totalTime)
{
    mExcludedQueryTime += queryTime;
    mExcludedTotalTime += totalTime;
}

uint32_t
DatabaseImpl::recentIdleDbPercent()
{
    std::chrono::nanoseconds query = totalQueryTime();
    query -= mLastIdleQueryTime;
    query -= mExcludedQueryTime;

    std::chrono::nanoseconds total = mApp.getClock().now() - mLastIdleTotalTime;
    total -= mExcludedTotalTime;

    uint32_t queryPercent =
        static_cast<uint32_t>((100 * query.count()) / total.count());
    uint32_t idlePercent = 100 - queryPercent;
    if (idlePercent > 100)
    {
        // This should never happen, but clocks are not perfectly well behaved.
        CLOG(WARNING, "Database") << "DB idle percent (" << idlePercent
                                  << ") over 100, limiting to 100";
        idlePercent = 100;
    }

    CLOG(DEBUG, "Database") << "Estimated DB idle: " << idlePercent << "%"
                            << " (query=" << query.count() << "ns"
                            << ", total=" << total.count() << "ns)";

    mLastIdleQueryTime = totalQueryTime();
    mLastIdleTotalTime = mApp.getClock().now();
    mExcludedQueryTime = std::chrono::nanoseconds(0);
    mExcludedTotalTime = std::chrono::nanoseconds(0);
    return idlePercent;
}

DBTimeExcluder::DBTimeExcluder(Application& app)
    : mApp(app)
    , mStartQueryTime(app.getDatabase().totalQueryTime())
    , mStartTotalTime(app.getClock().now())
{
}

DBTimeExcluder::~DBTimeExcluder()
{
    auto deltaQ = mApp.getDatabase().totalQueryTime() - mStartQueryTime;
    auto deltaT = mApp.getClock().now() - mStartTotalTime;
    mApp.getDatabase().excludeTime(deltaQ, deltaT);
}
}
