#include <exsysidgen/Generator.h>
#include <transactions/test/test_helper/CreateAccountTestHelper.h>
#include "main/Application.h"
#include "util/Timer.h"
#include "main/Config.h"
#include "overlay/LoopbackPeer.h"
#include "main/test.h"
#include "TxTests.h"
#include "ledger/BalanceHelperLegacy.h"
#include "ledger/LedgerDeltaImpl.h"
#include "test_helper/TestManager.h"
#include "transactions/test/test_helper/ManageExternalSystemAccountIDPoolEntryTestHelper.h"
#include "test/test_marshaler.h"

using namespace stellar;
using namespace stellar::txtest;

typedef std::unique_ptr<Application> appPtr;


TEST_CASE("manage external system account id pool entry", "[tx][manage_external_system_account_id_pool_entry]")
{
    auto const ERC20_TokenExternalSystemType = 4;
    Config const& cfg = getTestConfig(0, Config::TESTDB_POSTGRESQL);
    VirtualClock clock;
    auto const appPtr = Application::create(clock, cfg);
    auto& app = *appPtr;
    app.start();
    auto testManager = TestManager::make(app);

    LedgerDeltaImpl delta(app.getLedgerManager().getCurrentLedgerHeader(),
                          app.getDatabase());

    auto root = Account{ getRoot(), Salt(0) };

    ManageExternalSystemAccountIDPoolEntryTestHelper manageExternalSystemAccountIDPoolEntryTestHelper(testManager);

    SECTION("Invalid data")
    {
        manageExternalSystemAccountIDPoolEntryTestHelper.createExternalSystemAccountIdPoolEntry(root,
                                                        ERC20_TokenExternalSystemType, "",
                                                        ManageExternalSystemAccountIdPoolEntryResultCode::MALFORMED);
    }
    SECTION("Already exists")
    {
        manageExternalSystemAccountIDPoolEntryTestHelper.createExternalSystemAccountIdPoolEntry(root,
                                                        ERC20_TokenExternalSystemType, "Some data");
        manageExternalSystemAccountIDPoolEntryTestHelper.createExternalSystemAccountIdPoolEntry(root,
                                                        ERC20_TokenExternalSystemType, "Some data",
                                                    ManageExternalSystemAccountIdPoolEntryResultCode::ALREADY_EXISTS);
    }
    SECTION("Auto generated type")
    {
        manageExternalSystemAccountIDPoolEntryTestHelper.createExternalSystemAccountIdPoolEntry(root,
                                                        BitcoinExternalSystemType, "",
                                    ManageExternalSystemAccountIdPoolEntryResultCode::AUTO_GENERATED_TYPE_NOT_ALLOWED);
    }
    SECTION("Happy path")
    {
        auto res = manageExternalSystemAccountIDPoolEntryTestHelper.createExternalSystemAccountIdPoolEntry(root,
                                                        ERC20_TokenExternalSystemType, "Some data");
        SECTION("Delete")
        {
            manageExternalSystemAccountIDPoolEntryTestHelper.deleteExternalSystemAccountIdPoolEntry(root,
                                                      ManageExternalSystemAccountIdPoolEntryResultCode::SUCCESS,
                                                      res.success().poolEntryID);
        }
    }
    SECTION("Not found")
    {
        manageExternalSystemAccountIDPoolEntryTestHelper.deleteExternalSystemAccountIdPoolEntry(root,
                                                        ManageExternalSystemAccountIdPoolEntryResultCode::NOT_FOUND);
    }
}