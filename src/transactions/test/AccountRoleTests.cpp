#include "TxTests.h"
#include "crypto/SHA.h"
#include "ledger/AccountHelper.h"
#include "ledger/LedgerDeltaImpl.h"
#include "main/Application.h"
#include "main/test.h"
#include "overlay/LoopbackPeer.h"
#include "test/test_marshaler.h"
#include "test_helper/CreateAccountTestHelper.h"
#include "transactions/BindExternalSystemAccountIdOpFrame.h"
#include "transactions/ManageAccountRolePermissionOpFrame.h"
#include "transactions/test/test_helper/ManageAccountRolePermissionTestHelper.h"
#include "transactions/test/test_helper/ManageAccountRoleTestHelper.h"
#include "util/make_unique.h"

using namespace stellar;
using namespace stellar::txtest;

typedef std::unique_ptr<Application> appPtr;

static const std::string kRoleName = "some role";
static const std::string kAnotherRoleName = "another role";
static const uint64_t kInvalidRoleID = 999999;

TEST_CASE("Account role tests", "[tx][set_account_roles]")
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
    ManageAccountRoleTestHelper manageAccountRoleHelper(testManager);
    ManageAccountRolePermissionTestHelper setIdentityPolicyHelper(testManager);

    // create account for further tests
    auto accountKey = SecretKey::random();
    auto account = Account{accountKey, Salt(1)};

    createAccountTestHelper.applyTx(CreateAccountTestBuilder()
                                            .setSource(master)
                                            .setToPublicKey(accountKey.getPublicKey())
                                            .setType(AccountType::NOT_VERIFIED)
                                            .setRecovery(SecretKey::random().getPublicKey()));

    SECTION("Create account role")
    {
        ManageAccountRoleResult result = manageAccountRoleHelper.applySetAccountRole(
            account, manageAccountRoleHelper.createCreationOpInput(kRoleName));
        SECTION("Create another role")
        {
            manageAccountRoleHelper.applySetAccountRole(
                account,
                manageAccountRoleHelper.createCreationOpInput(kAnotherRoleName));
        }
        SECTION("Create another role with duplicate ID")
        {
            manageAccountRoleHelper.applySetAccountRole(
                account, manageAccountRoleHelper.createCreationOpInput(kRoleName));
        }
        SECTION("Create another role with duplicate name")
        {
            manageAccountRoleHelper.applySetAccountRole(
                account, manageAccountRoleHelper.createCreationOpInput(kRoleName));
        }
        SECTION("Delete account role")
        {
            manageAccountRoleHelper.applySetAccountRole(
                account, manageAccountRoleHelper.createDeletionOpInput(
                             result.success().accountRoleID));
        }
        SECTION("Delete non-existing account role")
        {
            manageAccountRoleHelper.applySetAccountRole(
                account,
                manageAccountRoleHelper.createDeletionOpInput(kInvalidRoleID),
                ManageAccountRoleResultCode::NOT_FOUND);
        }
    }
}