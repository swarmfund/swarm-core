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
#include "transactions/SetAccountRolePolicyOpFrame.h"
#include "transactions/test/test_helper/SetAccountRolePolicyTestHelper.h"
#include "transactions/test/test_helper/SetAccountRoleTestHelper.h"
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
    SetAccountRoleTestHelper setAccountRoleHelper(testManager);
    SetAccountRolePolicyTestHelper setIdentityPolicyHelper(testManager);

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
        SetAccountRoleResult result = setAccountRoleHelper.applySetAccountRole(
            account, setAccountRoleHelper.createCreationOpInput(kRoleName));
        SECTION("Create another role")
        {
            setAccountRoleHelper.applySetAccountRole(
                account,
                setAccountRoleHelper.createCreationOpInput(kAnotherRoleName));
        }
        SECTION("Create another role with duplicate ID")
        {
            setAccountRoleHelper.applySetAccountRole(
                account, setAccountRoleHelper.createCreationOpInput(kRoleName));
        }
        SECTION("Create another role with duplicate name")
        {
            setAccountRoleHelper.applySetAccountRole(
                account, setAccountRoleHelper.createCreationOpInput(kRoleName));
        }
        SECTION("Delete account role")
        {
            setAccountRoleHelper.applySetAccountRole(
                account, setAccountRoleHelper.createDeletionOpInput(
                             result.success().accountRoleID));
        }
        SECTION("Delete non-existing account role")
        {
            setAccountRoleHelper.applySetAccountRole(
                account,
                setAccountRoleHelper.createDeletionOpInput(kInvalidRoleID),
                SetAccountRoleResultCode::NOT_FOUND);
        }
        SECTION("Try to create role with old ID")
        {
            SetAccountRoleOp op;
            op.data.activate();
            op.id = result.success().accountRoleID;
            op.data->name = kAnotherRoleName;
            setAccountRoleHelper.applySetAccountRole(
                account, op, SetAccountRoleResultCode::MALFORMED);
        }
    }
}