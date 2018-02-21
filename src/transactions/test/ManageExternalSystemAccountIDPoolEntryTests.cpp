#include "main/Application.h"
#include "util/Timer.h"
#include "main/Config.h"
#include "overlay/LoopbackPeer.h"
#include "main/test.h"
#include "TxTests.h"
#include "ledger/BalanceHelper.h"
#include "ledger/LedgerDelta.h"
#include "test_helper/TestManager.h"
#include "transactions/test/test_helper/ManageExternalSystemAccountIDPoolEntryTestHelper.h"
#include "test/test_marshaler.h"

using namespace stellar;
using namespace stellar::txtest;

typedef std::unique_ptr<Application> appPtr;


TEST_CASE("manage external system account id pool entry", "[tx][manage_external_system_account_id_pool_entry]")
{
    Config const& cfg = getTestConfig(0, Config::TESTDB_POSTGRESQL);
    VirtualClock clock;
    auto const appPtr = Application::create(clock, cfg);
    auto& app = *appPtr;
    app.start();
    auto testManager = TestManager::make(app);

    LedgerDelta delta(app.getLedgerManager().getCurrentLedgerHeader(),
                      app.getDatabase());

    auto root = Account{ getRoot(), Salt(0) };

    ManageExternalSystemAccountIDPoolEntryTestHelper manageExternalSystemAccountIDPoolEntryTestHelper(testManager);

    SECTION("Invalid data")
    {
        manageExternalSystemAccountIDPoolEntryTestHelper.createExternalSystemAccountIdPoolEntry(root,
                                                        ExternalSystemType::BITCOIN, "",
                                                        ManageExternalSystemAccountIdPoolEntryResultCode::MALFORMED);
    }
    SECTION("Already exists")
    {
        manageExternalSystemAccountIDPoolEntryTestHelper.createExternalSystemAccountIdPoolEntry(root,
                                                                            ExternalSystemType::BITCOIN, "Some data");
        manageExternalSystemAccountIDPoolEntryTestHelper.createExternalSystemAccountIdPoolEntry(root,
                                                                            ExternalSystemType::BITCOIN, "Some data",
                                                    ManageExternalSystemAccountIdPoolEntryResultCode::ALREADY_EXISTS);
    }
    SECTION("Happy path")
    {
        manageExternalSystemAccountIDPoolEntryTestHelper.createExternalSystemAccountIdPoolEntry(root,
                                                                            ExternalSystemType::BITCOIN, "Some data");
    }
}