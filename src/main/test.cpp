// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include <cstdlib>
#include "test.h"
#include "StellarCoreVersion.h"
#include "main/Config.h"
#include "util/make_unique.h"
#include <time.h>
#include "util/Logging.h"
#include "util/TmpDir.h"
#include "crypto/ByteSlice.h"

#ifdef _WIN32
#include <process.h>
#define GETPID _getpid
#include <direct.h>
#else
#include <unistd.h>
#define GETPID getpid
#include <sys/stat.h>
#endif

#define CATCH_CONFIG_RUNNER
#include "test/test_marshaler.h"

namespace stellar
{

static std::vector<std::string> gTestMetrics;
static std::vector<std::unique_ptr<Config>> gTestCfg[Config::TESTDB_MODES];
static std::vector<TmpDir> gTestRoots;

bool force_sqlite = (std::getenv("STELLAR_FORCE_SQLITE") != nullptr);
const char* db_conn_str = std::getenv("STELLAR_TX_TEST_DB");

Config
getTestConfig(int instanceNumber, Config::TestDbMode mode)
{
    if (mode == Config::TESTDB_DEFAULT)
    {
        // we don't maintain sqlite anymore, but if we will,
        // you can change this by enabling the appropriate line below
        // mode = Config::TESTDB_IN_MEMORY_SQLITE;
        // mode = Config::TESTDB_ON_DISK_SQLITE;
        mode = Config::TESTDB_POSTGRESQL;
    }
    auto& cfgs = gTestCfg[mode];
    if (cfgs.size() <= static_cast<size_t>(instanceNumber))
    {
        cfgs.resize(instanceNumber + 1);
    }

    if (!cfgs[instanceNumber])
    {
        gTestRoots.emplace_back("stellar-core-test");

        std::string rootDir = gTestRoots.back().getName();
        rootDir += "/";

        cfgs[instanceNumber] = stellar::make_unique<Config>();
        Config& thisConfig = *cfgs[instanceNumber];

        std::ostringstream sstream;

        sstream << "stellar" << instanceNumber << ".log";
        thisConfig.LOG_FILE_PATH = sstream.str();
        thisConfig.BUCKET_DIR_PATH = rootDir + "bucket";
        thisConfig.TMP_DIR_PATH = rootDir + "tmp";

        thisConfig.PARANOID_MODE = true;
        thisConfig.ALLOW_LOCALHOST_FOR_TESTING = true;
		thisConfig.DESIRED_MAX_TX_PER_LEDGER = 2000;

        // Tests are run in standalone by default, meaning that no external
        // listening interfaces are opened (all sockets must be manually created
        // and connected loopback sockets), no external connections are
        // attempted.
        thisConfig.RUN_STANDALONE = true;
        thisConfig.FORCE_SCP = true;

		thisConfig.masterID = getMasterKP().getPublicKey();
		thisConfig.commissionID = getCommissionKP().getPublicKey();

        thisConfig.PEER_PORT =
            static_cast<unsigned short>(DEFAULT_PEER_PORT + instanceNumber * 2);
        thisConfig.HTTP_PORT = static_cast<unsigned short>(
            DEFAULT_PEER_PORT + instanceNumber * 2 + 1);

        // We set a secret key by default as FORCE_SCP is true by
        // default and we do need a NODE_SEED to start a new network
        thisConfig.NODE_SEED = SecretKey::random();
        thisConfig.NODE_IS_VALIDATOR = true;

        // single node setup
        thisConfig.QUORUM_SET.validators.push_back(
            thisConfig.NODE_SEED.getPublicKey());
        thisConfig.QUORUM_SET.threshold = 1;
        thisConfig.UNSAFE_QUORUM = true;

        thisConfig.NETWORK_PASSPHRASE = "(V) (;,,;) (V)";
        thisConfig.BASE_EXCHANGE_NAME = "Base exchange";
        thisConfig.TX_EXPIRATION_PERIOD = INT64_MAX / 2;
        thisConfig.MAX_INVOICES_FOR_RECEIVER_ACCOUNT = 100;
        auto extendedPublicKey = "xpub661MyMwAqRbcFW31YEwpkMuc5THy2PSt5bDMsktWQcFF8syAmRUapSCGu8ED9W6oDMSgv6Zz8idoc4a6mr8BDzTJY47LJhkJ8UB7WEGuduB";
        thisConfig.BTC_ADDRESS_ROOT = extendedPublicKey;
        thisConfig.ETH_ADDRESS_ROOT = extendedPublicKey;

        // disable NTP - travis-ci does not allow network access:
        // The container-based, OSX, and GCE (both Precise and Trusty) builds do not currently have IPv6 connectivity.
        thisConfig.NTP_SERVER.clear();

        std::ostringstream dbname;
        switch (mode)
        {
        case Config::TESTDB_IN_MEMORY_SQLITE:
            dbname << "sqlite3://:memory:";
            break;
        case Config::TESTDB_ON_DISK_SQLITE:
            dbname << "sqlite3://" << rootDir << "test" << instanceNumber
                   << ".db";
            break;
#ifdef USE_POSTGRES
        case Config::TESTDB_POSTGRESQL:
            if (db_conn_str != nullptr)
            {
                dbname << db_conn_str;
            } else {
                std::string dbNumber = "";
                if (instanceNumber != 0)
                    dbNumber = std::to_string(instanceNumber);
                dbname << "postgresql://dbname=stellar_test10" << dbNumber << " user=postgres password=password host=localhost";
            }
            break;
#endif
        default:
            abort();
        }
        thisConfig.DATABASE = dbname.str();
        thisConfig.REPORT_METRICS = gTestMetrics;

		thisConfig.validateConfig();
    }
    return *cfgs[instanceNumber];
}



SecretKey getMasterKP()
{
	return getAccountSecret("master");
}
SecretKey getIssuanceKP()
{
	return getAccountSecret("issuance");
}

SecretKey getCommissionKP()
{
	return getAccountSecret("commission");
}

SecretKey getAccountSecret(const char* n)
{
	// stretch seed to 32 bytes
	std::string seed(n);
	while (seed.size() < 32)
		seed += '.';
	return SecretKey::fromSeed(seed);
}

int
test(int argc, char* argv[], el::Level ll,
     std::vector<std::string> const& metrics)
{
    gTestMetrics = metrics;
    Config const& cfg = getTestConfig();
    Logging::setFmt("<test>");
    Logging::setLoggingToFile(cfg.LOG_FILE_PATH);
    Logging::setLogLevel(ll, nullptr);
    LOG(INFO) << "Testing stellar-core " << STELLAR_CORE_VERSION;
    LOG(INFO) << "Logging to " << cfg.LOG_FILE_PATH;

    ::testing::GTEST_FLAG(throw_on_failure) = true;
    ::testing::InitGoogleMock(&argc, argv);

    return Catch::Session().run(argc, argv);
}
}
