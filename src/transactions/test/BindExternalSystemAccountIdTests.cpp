#include "main/Application.h"
#include "util/Timer.h"
#include "main/Config.h"
#include "overlay/LoopbackPeer.h"
#include "main/test.h"
#include "TxTests.h"
#include "ledger/BalanceHelper.h"
#include "test_helper/BindExternalSystemAccountIdTestHelper.h"
#include "test_helper/CreateAccountTestHelper.h"
#include "test_helper/ManageExternalSystemAccountIDPoolEntryTestHelper.h"
#include "test/test_marshaler.h"

using namespace stellar;
using namespace stellar::txtest;

typedef std::unique_ptr<Application> appPtr;


TEST_CASE("bind external system account_id", "[tx][bind_external_system_account_id]")
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

    BindExternalSystemAccountIdTestHelper bindExternalSystemAccountIdTestHelper(testManager);
    CreateAccountTestHelper createAccountTestHelper(testManager);
    ManageExternalSystemAccountIDPoolEntryTestHelper manageExternalSystemAccountIDPoolEntryTestHelper(testManager);

    auto account = Account { SecretKey::random(), Salt(0) };
    createAccountTestHelper.applyCreateAccountTx(root, account.key.getPublicKey(), AccountType::GENERAL);

    testManager->advanceToTime(12*60*60);

    SECTION("Happy path")
    {
        manageExternalSystemAccountIDPoolEntryTestHelper.createExternalSystemAccountIdPoolEntry(root,
                                                                                            ExternalSystemType::BITCOIN,
                                                                                            "Some data");
        manageExternalSystemAccountIDPoolEntryTestHelper.createExternalSystemAccountIdPoolEntry(root,
                                                                                            ExternalSystemType::ETHEREUM,
                                                                                            "Ethereum data");

        bindExternalSystemAccountIdTestHelper.applyBindExternalSystemAccountIdTx(account, ExternalSystemType::BITCOIN);

        SECTION("Account already has external system account id of this type")
        {
            manageExternalSystemAccountIDPoolEntryTestHelper.createExternalSystemAccountIdPoolEntry(root,
                                                                                            ExternalSystemType::BITCOIN,
                                                                                            "Some another data");
            bindExternalSystemAccountIdTestHelper.applyBindExternalSystemAccountIdTx(account,
                                                                 ExternalSystemType::BITCOIN,
                                                                 BindExternalSystemAccountIdResultCode::ALREADY_HAS);
        }
        SECTION("Account binds system account id of another type")
        {
            bindExternalSystemAccountIdTestHelper.applyBindExternalSystemAccountIdTx(account,
                                                                                     ExternalSystemType::ETHEREUM);
        }
    }

    SECTION("Empty pool")
    {
        bindExternalSystemAccountIdTestHelper.applyBindExternalSystemAccountIdTx(account, ExternalSystemType::BITCOIN,
                                                             BindExternalSystemAccountIdResultCode::NO_AVAILABLE_ID);
    }

    SECTION("No external system account ids of this type in pool")
    {
        manageExternalSystemAccountIDPoolEntryTestHelper.createExternalSystemAccountIdPoolEntry(root,
                                                                                            ExternalSystemType::BITCOIN,
                                                                                            "Some data");
        bindExternalSystemAccountIdTestHelper.applyBindExternalSystemAccountIdTx(account, ExternalSystemType::ETHEREUM,
                                                             BindExternalSystemAccountIdResultCode::NO_AVAILABLE_ID);
    }

    SECTION("All external system account ids of this type are bound")
    {
        auto binder = Account { SecretKey::random(), Salt(0) };
        createAccountTestHelper.applyCreateAccountTx(root, binder.key.getPublicKey(), AccountType::GENERAL);

        manageExternalSystemAccountIDPoolEntryTestHelper.createExternalSystemAccountIdPoolEntry(root,
                                                                                            ExternalSystemType::BITCOIN,
                                                                                            "Some data");

        bindExternalSystemAccountIdTestHelper.applyBindExternalSystemAccountIdTx(binder, ExternalSystemType::BITCOIN);

        bindExternalSystemAccountIdTestHelper.applyBindExternalSystemAccountIdTx(account, ExternalSystemType::BITCOIN,
                                                             BindExternalSystemAccountIdResultCode::NO_AVAILABLE_ID);
    }
    SECTION("Bind expired external system account id")
    {
        auto binder = Account {SecretKey::random(), Salt(0)};
        createAccountTestHelper.applyCreateAccountTx(root, binder.key.getPublicKey(), AccountType::GENERAL);

        manageExternalSystemAccountIDPoolEntryTestHelper.createExternalSystemAccountIdPoolEntry(root,
                                                                                                ExternalSystemType::BITCOIN,
                                                                                                "Some data");

        bindExternalSystemAccountIdTestHelper.applyBindExternalSystemAccountIdTx(binder, ExternalSystemType::BITCOIN);

        testManager->advanceToTime(48 * 60 * 60);

        bindExternalSystemAccountIdTestHelper.applyBindExternalSystemAccountIdTx(account, ExternalSystemType::BITCOIN);
    }
}