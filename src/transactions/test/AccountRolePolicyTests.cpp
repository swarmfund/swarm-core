#include "TxTests.h"
#include "crypto/SHA.h"
#include "ledger/AccountHelper.h"
#include "ledger/AccountRolePolicyHelper.h"
#include "ledger/AssetHelper.h"
#include "ledger/LedgerDeltaImpl.h"
#include "main/Application.h"
#include "main/test.h"
#include "overlay/LoopbackPeer.h"
#include "test/test_marshaler.h"
#include "test_helper/CreateAccountTestHelper.h"
#include "test_helper/ManageAssetPairTestHelper.h"
#include "test_helper/ManageAssetTestHelper.h"
#include "transactions/ManageAccountRolePolicyOpFrame.h"
#include "transactions/test/test_helper/SetAccountRolePolicyTestHelper.h"
#include "util/make_unique.h"
#include <transactions/test/test_helper/SetAccountRoleTestHelper.h>

using namespace stellar;
using namespace stellar::txtest;

typedef std::unique_ptr<Application> appPtr;

static const std::string kResourceID = "some resource type";
static const std::string kActionID = "some action";

TEST_CASE("Set role policy", "[tx][set_account_role_policies]")
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
    SetAccountRoleTestHelper setAccountRoleTestHelper(testManager);
    SetAccountRolePolicyTestHelper manageAccountRolePolicyTestHelper(testManager);

    // create account for further tests
    auto accountKey = SecretKey::random();
    auto account = Account{accountKey, Salt(1)};

    createAccountTestHelper.applyCreateAccountTx(
        master, accountKey.getPublicKey(), AccountType::GENERAL);

    // create account role
    auto accountRoleID = setAccountRoleTestHelper.applySetAccountRole(
        master, setAccountRoleTestHelper.createCreationOpInput("regular")).success().accountRoleID;

    auto data = PolicyDetails{kResourceID, kActionID};

    SECTION("Successful creation")
    {
        auto policyEntry =
            manageAccountRolePolicyTestHelper.createAccountRolePolicyEntry(
                accountRoleID, accountKey.getPublicKey(), &data);
        manageAccountRolePolicyTestHelper.applySetIdentityPolicyTx(
            account, policyEntry, ManageAccountRolePolicyOpAction::CREATE,
            ManageAccountRolePolicyResultCode::SUCCESS);
    }
    SECTION("Successful updating")
    {
        auto policyEntry =
            manageAccountRolePolicyTestHelper.createAccountRolePolicyEntry(
                accountRoleID, accountKey.getPublicKey(), &data);
        manageAccountRolePolicyTestHelper.applySetIdentityPolicyTx(
            account, policyEntry, ManageAccountRolePolicyOpAction::CREATE,
            ManageAccountRolePolicyResultCode::SUCCESS);
        // update entry
        policyEntry.action = "NewAction";
        manageAccountRolePolicyTestHelper.applySetIdentityPolicyTx(
            account, policyEntry, ManageAccountRolePolicyOpAction::UPDATE,
            ManageAccountRolePolicyResultCode::SUCCESS);
    }
    SECTION("Invalid resource")
    {
        auto policyEntryResourceAndAction =
            manageAccountRolePolicyTestHelper.createAccountRolePolicyEntry(
                accountRoleID, accountKey.getPublicKey(), &data);
        policyEntryResourceAndAction.resource = "";
        policyEntryResourceAndAction.action = kActionID;
        manageAccountRolePolicyTestHelper.applySetIdentityPolicyTx(
            account, policyEntryResourceAndAction, ManageAccountRolePolicyOpAction::CREATE,
            ManageAccountRolePolicyResultCode::EMPTY_RESOURCE);
    }
    SECTION("Invalid action")
    {
        auto policyEntryResourceAndAction =
            manageAccountRolePolicyTestHelper.createAccountRolePolicyEntry(
                accountRoleID, accountKey.getPublicKey(), &data);
        policyEntryResourceAndAction.resource = kResourceID;
        policyEntryResourceAndAction.action = "";
        manageAccountRolePolicyTestHelper.applySetIdentityPolicyTx(
            account, policyEntryResourceAndAction, ManageAccountRolePolicyOpAction::CREATE,
            ManageAccountRolePolicyResultCode::EMPTY_ACTION);
    }
    SECTION("Identity policy not found when try to delete it")
    {
        auto policyEntry =
            manageAccountRolePolicyTestHelper.createAccountRolePolicyEntry(
                accountRoleID, accountKey.getPublicKey(), &data);
        manageAccountRolePolicyTestHelper.applySetIdentityPolicyTx(
            account, policyEntry, ManageAccountRolePolicyOpAction::REMOVE,
            ManageAccountRolePolicyResultCode::NOT_FOUND);
    }
    SECTION("Successful deletion")
    {
        auto policyEntry =
            manageAccountRolePolicyTestHelper.createAccountRolePolicyEntry(
                accountRoleID, accountKey.getPublicKey(), &data);
        // create
        manageAccountRolePolicyTestHelper.applySetIdentityPolicyTx(
            account, policyEntry, ManageAccountRolePolicyOpAction::CREATE,
            ManageAccountRolePolicyResultCode::SUCCESS);
        // delete
        policyEntry.accountRolePolicyID = 1;
        manageAccountRolePolicyTestHelper.applySetIdentityPolicyTx(
            account, policyEntry, ManageAccountRolePolicyOpAction::REMOVE,
            ManageAccountRolePolicyResultCode::SUCCESS);
    }
    SECTION("Create without data")
    {
        auto policyEntry =
            manageAccountRolePolicyTestHelper.createAccountRolePolicyEntry(
                accountRoleID, accountKey.getPublicKey(), &data);
        // create
        manageAccountRolePolicyTestHelper.applySetIdentityPolicyTx(
            account, policyEntry, ManageAccountRolePolicyOpAction::CREATE,
            ManageAccountRolePolicyResultCode::SUCCESS);
        // delete
        policyEntry.accountRolePolicyID = 1;
        manageAccountRolePolicyTestHelper.applySetIdentityPolicyTx(
            account, policyEntry, ManageAccountRolePolicyOpAction::REMOVE,
            ManageAccountRolePolicyResultCode::SUCCESS);
    }
}