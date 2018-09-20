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
#include "transactions/SetAccountRolePolicyOpFrame.h"
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
    SetAccountRolePolicyTestHelper setAccountRolePolicyTestHelper(testManager);

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
            setAccountRolePolicyTestHelper.createAccountRolePolicyEntry(
                accountRoleID, accountKey.getPublicKey(), &data);
        setAccountRolePolicyTestHelper.applySetIdentityPolicyTx(
            account, policyEntry, false,
            SetAccountRolePolicyResultCode::SUCCESS);
    }
    SECTION("Successful updating")
    {
        auto policyEntry =
            setAccountRolePolicyTestHelper.createAccountRolePolicyEntry(
                accountRoleID, accountKey.getPublicKey(), &data);
        setAccountRolePolicyTestHelper.applySetIdentityPolicyTx(
            account, policyEntry, false,
            SetAccountRolePolicyResultCode::SUCCESS);
        // update entry
        policyEntry.action = "NewAction";
        setAccountRolePolicyTestHelper.applySetIdentityPolicyTx(
            account, policyEntry, false,
            SetAccountRolePolicyResultCode::SUCCESS);
    }
    SECTION("Invalid resource")
    {
        auto policyEntryResourceAndAction =
            setAccountRolePolicyTestHelper.createAccountRolePolicyEntry(
                accountRoleID, accountKey.getPublicKey(), &data);
        policyEntryResourceAndAction.resource = "";
        policyEntryResourceAndAction.action = kActionID;
        setAccountRolePolicyTestHelper.applySetIdentityPolicyTx(
            account, policyEntryResourceAndAction, false,
            SetAccountRolePolicyResultCode::MALFORMED);
    }
    SECTION("Invalid action")
    {
        auto policyEntryResourceAndAction =
            setAccountRolePolicyTestHelper.createAccountRolePolicyEntry(
                accountRoleID, accountKey.getPublicKey(), &data);
        policyEntryResourceAndAction.resource = kResourceID;
        policyEntryResourceAndAction.action = "";
        setAccountRolePolicyTestHelper.applySetIdentityPolicyTx(
            account, policyEntryResourceAndAction, false,
            SetAccountRolePolicyResultCode::MALFORMED);
    }
    SECTION("Identity policy not found when try to delete it")
    {
        auto policyEntry =
            setAccountRolePolicyTestHelper.createAccountRolePolicyEntry(
                accountRoleID, accountKey.getPublicKey(), &data);
        setAccountRolePolicyTestHelper.applySetIdentityPolicyTx(
            account, policyEntry, true,
            SetAccountRolePolicyResultCode::NOT_FOUND);
    }
    SECTION("Successful deletion")
    {
        auto policyEntry =
            setAccountRolePolicyTestHelper.createAccountRolePolicyEntry(
                accountRoleID, accountKey.getPublicKey(), &data);
        // create
        setAccountRolePolicyTestHelper.applySetIdentityPolicyTx(
            account, policyEntry, false,
            SetAccountRolePolicyResultCode::SUCCESS);
        // delete
        // assign 1 due to ids increases linear from 1 to uint64_max
        policyEntry.accountRolePolicyID = 1;
        setAccountRolePolicyTestHelper.applySetIdentityPolicyTx(
            account, policyEntry, true,
            SetAccountRolePolicyResultCode::SUCCESS);
    }
    SECTION("Create without data")
    {
        auto policyEntry =
            setAccountRolePolicyTestHelper.createAccountRolePolicyEntry(
                accountRoleID, accountKey.getPublicKey(), &data);
        // create
        setAccountRolePolicyTestHelper.applySetIdentityPolicyTx(
            account, policyEntry, false,
            SetAccountRolePolicyResultCode::SUCCESS);
        // delete
        policyEntry.accountRolePolicyID = 1;
        setAccountRolePolicyTestHelper.applySetIdentityPolicyTx(
            account, policyEntry, true,
            SetAccountRolePolicyResultCode::SUCCESS);
    }
}