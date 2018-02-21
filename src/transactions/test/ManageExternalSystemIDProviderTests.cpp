#include "main/Application.h"
#include "util/Timer.h"
#include "main/Config.h"
#include "overlay/LoopbackPeer.h"
#include "main/test.h"
#include "TxTests.h"
#include "ledger/BalanceHelper.h"
#include "ledger/LedgerDelta.h"
#include "test_helper/TestManager.h"
#include "test_helper/ManageExternalSystemIDProviderTestHelper.h"
#include "test/test_marshaler.h"

using namespace stellar;
using namespace stellar::txtest;

typedef std::unique_ptr<Application> appPtr;


TEST_CASE("manage external system id provider", "[tx][manage_external_system_id_provider]")
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

    ManageExternalSystemIDProviderTestHelper manageExternalSystemIDProviderTestHelper(testManager);

    SECTION("Invalid data")
    {
        manageExternalSystemIDProviderTestHelper.applyManageExternalSystemIDProviderTx(root,
                                                                   ExternalSystemType::BITCOIN, "",
                                                                   ManageExternalSystemIdProviderAction::CREATE,
                                                                   ManageExternalSystemIdProviderResultCode::MALFORMED);
    }
    SECTION("Already exists")
    {
        manageExternalSystemIDProviderTestHelper.applyManageExternalSystemIDProviderTx(root,
                                                                                       ExternalSystemType::BITCOIN,
                                                                                       "Some data");
        manageExternalSystemIDProviderTestHelper.applyManageExternalSystemIDProviderTx(root,
                                                                   ExternalSystemType::BITCOIN, "Some data",
                                                                   ManageExternalSystemIdProviderAction::CREATE,
                                                                   ManageExternalSystemIdProviderResultCode::ALREADY_EXISTS);
    }
    SECTION("Happy path")
    {
        manageExternalSystemIDProviderTestHelper.applyManageExternalSystemIDProviderTx(root,
                                                                                       ExternalSystemType::BITCOIN,
                                                                                       "Some data");
    }
}