#include "TxTests.h"
#include "crypto/SHA.h"
#include "ledger/AccountHelper.h"
#include "ledger/AccountRolePermissionHelperImpl.h"
#include "ledger/AssetHelper.h"
#include "ledger/LedgerDeltaImpl.h"
#include "main/Application.h"
#include "main/test.h"
#include "overlay/LoopbackPeer.h"
#include "test/test_marshaler.h"
#include "test_helper/CreateAccountTestHelper.h"
#include "test_helper/ManageAssetPairTestHelper.h"
#include "test_helper/ManageAssetTestHelper.h"
#include "transactions/ManageAccountRolePermissionOpFrame.h"
#include "transactions/test/test_helper/ManageAccountRolePermissionTestHelper.h"
#include "util/make_unique.h"
#include <transactions/test/test_helper/ManageAccountRoleTestHelper.h>

using namespace stellar;
using namespace stellar::txtest;

typedef std::unique_ptr<Application> appPtr;

static const OperationType kOperationType = OperationType::CREATE_AML_ALERT;

TEST_CASE("Set role policy", "[tx][set_account_role_permissions]")
{
    Config const& cfg = getTestConfig(0, Config::TESTDB_POSTGRESQL);

    VirtualClock clock;
    Application::pointer appPtr = Application::create(clock, cfg);
    Application& app = *appPtr;
    app.start();
    TestManager::upgradeToCurrentLedgerVersion(app);

    Database& db = app.getDatabase();

    auto testManager = TestManager::make(app);

    LedgerDeltaImpl delta(app.getLedgerManager().getCurrentLedgerHeader(),
                          app.getDatabase());

    // set up world
    auto master = Account{getRoot(), Salt(1)};

    CreateAccountTestHelper createAccountTestHelper(testManager);
    ManageAccountRoleTestHelper setAccountRoleTestHelper(testManager);
    ManageAccountRolePermissionTestHelper manageAccountRolePolicyTestHelper(
        testManager);

    // create account for further tests
    auto accountKey = SecretKey::random();
    auto account = Account{accountKey, Salt(1)};

    createAccountTestHelper.applyCreateAccountTx(
        master, accountKey.getPublicKey(), AccountType::GENERAL);

    // create account role
    auto accountRoleID =
        setAccountRoleTestHelper
            .applySetAccountRole(
                master,
                setAccountRoleTestHelper.createCreationOpInput("regular"))
            .success()
            .accountRoleID;

    SECTION("Successful creation")
    {
        auto policyEntry =
            manageAccountRolePolicyTestHelper.createAccountRolePermissionEntry(
                accountRoleID, kOperationType);
        manageAccountRolePolicyTestHelper.applySetIdentityPermissionTx(
            account, policyEntry, ManageAccountRolePermissionOpAction::CREATE,
            ManageAccountRolePermissionResultCode::SUCCESS);
        SECTION("Successful updating")
        {
            policyEntry.opType = OperationType::CREATE_ACCOUNT;
            manageAccountRolePolicyTestHelper.applySetIdentityPermissionTx(
                account, policyEntry,
                ManageAccountRolePermissionOpAction::UPDATE,
                ManageAccountRolePermissionResultCode::SUCCESS);
        }
        SECTION("Successful deletion")
        {
            manageAccountRolePolicyTestHelper.applySetIdentityPermissionTx(
                account, policyEntry,
                ManageAccountRolePermissionOpAction::REMOVE,
                ManageAccountRolePermissionResultCode::SUCCESS);
        }
    }
    SECTION("Invalid operation type")
    {
        auto policyEntryResourceAndAction =
            manageAccountRolePolicyTestHelper.createAccountRolePermissionEntry(
                accountRoleID, static_cast<OperationType>(-1));
        REQUIRE_THROWS(manageAccountRolePolicyTestHelper.applySetIdentityPermissionTx(
            account, policyEntryResourceAndAction,
            ManageAccountRolePermissionOpAction::CREATE));
    }
    SECTION("Identity policy not found when trying to delete it")
    {
        auto policyEntry =
            manageAccountRolePolicyTestHelper.createAccountRolePermissionEntry(
                accountRoleID, kOperationType);
        manageAccountRolePolicyTestHelper.applySetIdentityPermissionTx(
            account, policyEntry, ManageAccountRolePermissionOpAction::REMOVE,
            ManageAccountRolePermissionResultCode::NOT_FOUND);
    }
}