
#include "main/Application.h"
#include "overlay/LoopbackPeer.h"
#include "util/make_unique.h"
#include "main/test.h"
#include "TxTests.h"
#include "ledger/LedgerDelta.h"
#include "ledger/IdentityPolicyHelper.h"
#include "transactions/SetIdentityPolicyOpFrame.h"
#include "crypto/SHA.h"
#include "test_helper/CreateAccountTestHelper.h"
#include "test_helper/ManageAssetTestHelper.h"
#include "test_helper/ManageAssetPairTestHelper.h"
#include "test_helper/SetIdentityPolicyTestHelper.h"
#include "test/test_marshaler.h"
#include "ledger/AssetHelper.h"
#include "ledger/AccountHelper.h"

using namespace stellar;
using namespace stellar::txtest;

typedef std::unique_ptr<Application> appPtr;

TEST_CASE("Set identity policy", "[tx][set_identity_policy]") {
    Config const &cfg = getTestConfig(0, Config::TESTDB_POSTGRESQL);

    VirtualClock clock;
    Application::pointer appPtr = Application::create(clock, cfg);
    Application &app = *appPtr;
    app.start();
    TestManager::upgradeToCurrentLedgerVersion(app);

    Database &db = app.getDatabase();

    auto testManager = TestManager::make(app);

    LedgerDelta delta(app.getLedgerManager().getCurrentLedgerHeader(),
                      app.getDatabase());

    // set up world
    auto master = Account{getRoot(), Salt(1)};

    const auto identityPolicyHelper = IdentityPolicyHelper::Instance();

    CreateAccountTestHelper createAccountTestHelper(testManager);
    SetIdentityPolicyTestHelper setIdentityPolicyTestHelper(testManager);

    // create account for further tests
    auto accountKey = SecretKey::random();
    auto account = Account{accountKey, Salt(1)};

    createAccountTestHelper.applyCreateAccountTx(master, accountKey.getPublicKey(), AccountType::GENERAL);

    auto data = SetIdentityPolicyData(PRIORITY_USER_MIN,
                                      "resource_type:::",
                                      "SomeAction",
                                      Effect::DENY,
                                      SetIdentityPolicyData::_ext_t{});

    SECTION("Successful creation") {
        auto policyEntry =
            setIdentityPolicyTestHelper.createIdentityPolicyEntry(0,
                                                                  accountKey.getPublicKey(),
                                                                  &data);
        setIdentityPolicyTestHelper.applySetIdentityPolicyTx(account,
                                                             policyEntry,
                                                             false,
                                                             SetIdentityPolicyResultCode::SUCCESS);
    }
    SECTION("Successful updating") {
        auto policyEntry =
                setIdentityPolicyTestHelper.createIdentityPolicyEntry(0,
                                                                      accountKey.getPublicKey(),
                                                                      &data);
        setIdentityPolicyTestHelper.applySetIdentityPolicyTx(account,
                                                             policyEntry,
                                                             false,
                                                             SetIdentityPolicyResultCode::SUCCESS);
        // update entry
        policyEntry.action = "NewAction";
        setIdentityPolicyTestHelper.applySetIdentityPolicyTx(account,
                                                             policyEntry,
                                                             false,
                                                             SetIdentityPolicyResultCode::SUCCESS);
    }
    SECTION("Ivalid action and/or priority and/or resource(is empty string)") {
        // Check user priority
        auto policyEntryPriorityUser =
            setIdentityPolicyTestHelper.createIdentityPolicyEntry(0,
                                                                  accountKey.getPublicKey(),
                                                                  &data);
        policyEntryPriorityUser.priority = PRIORITY_ADMIN_MIN;
        setIdentityPolicyTestHelper.applySetIdentityPolicyTx(account,
                                                             policyEntryPriorityUser,
                                                             false,
                                                             SetIdentityPolicyResultCode::MALFORMED);
        policyEntryPriorityUser.priority = PRIORITY_ADMIN_MAX + 1;
        setIdentityPolicyTestHelper.applySetIdentityPolicyTx(account,
                                                             policyEntryPriorityUser,
                                                             false,
                                                             SetIdentityPolicyResultCode::MALFORMED);
        // Check master priority
        auto policyEntryPriorityMaster =
                setIdentityPolicyTestHelper.createIdentityPolicyEntry(0,
                                                                      getRoot().getPublicKey(),
                                                                      &data);
        policyEntryPriorityMaster.priority = PRIORITY_USER_MIN;
        setIdentityPolicyTestHelper.applySetIdentityPolicyTx(master,
                                                             policyEntryPriorityMaster,
                                                             false,
                                                             SetIdentityPolicyResultCode::MALFORMED);

        // Invalid resource
        auto policyEntryResourceAndAction =
            setIdentityPolicyTestHelper.createIdentityPolicyEntry(0,
                                                                  accountKey.getPublicKey(),
                                                                  &data);
        policyEntryResourceAndAction.resource = "";
        setIdentityPolicyTestHelper.applySetIdentityPolicyTx(account,
                                                             policyEntryResourceAndAction,
                                                             false,
                                                             SetIdentityPolicyResultCode::MALFORMED);
        policyEntryResourceAndAction.resource = "resource_type:::";
        policyEntryResourceAndAction.action = "";
        setIdentityPolicyTestHelper.applySetIdentityPolicyTx(account,
                                                             policyEntryResourceAndAction,
                                                             false,
                                                             SetIdentityPolicyResultCode::MALFORMED);
    }
    SECTION("Too many policies per account") {
        auto policyEntry =
                setIdentityPolicyTestHelper.createIdentityPolicyEntry(0,
                                                                      accountKey.getPublicKey(),
                                                                      &data);
        for (int i = 0; i < app.getMaxIdentityPoliciesPerAccount(); i++)
        {
            setIdentityPolicyTestHelper.applySetIdentityPolicyTx(account,
                                                                 policyEntry,
                                                                 false,
                                                                 SetIdentityPolicyResultCode::SUCCESS);
        }
        setIdentityPolicyTestHelper.applySetIdentityPolicyTx(account,
                                                             policyEntry,
                                                             false,
                                                             SetIdentityPolicyResultCode::POLICIES_LIMIT_EXCEED);
    }
    SECTION("Identity policy not found when try to delete it") {
        auto policyEntry =
            setIdentityPolicyTestHelper.createIdentityPolicyEntry(0,
                                                                  accountKey.getPublicKey(),
                                                                  &data);
        setIdentityPolicyTestHelper.applySetIdentityPolicyTx(account,
                                                             policyEntry,
                                                             true,
                                                             SetIdentityPolicyResultCode::NOT_FOUND);
    }
    SECTION("Successful deletion") {
        auto policyEntry =
            setIdentityPolicyTestHelper.createIdentityPolicyEntry(0,
                                                                  accountKey.getPublicKey(),
                                                                  &data);
        // create
        setIdentityPolicyTestHelper.applySetIdentityPolicyTx(account,
                                                             policyEntry,
                                                             false,
                                                             SetIdentityPolicyResultCode::SUCCESS);
        // delete
        // assign 1 due to ids increases linear from 1 to uint64_max
        policyEntry.id = 1;
        setIdentityPolicyTestHelper.applySetIdentityPolicyTx(account,
                                                             policyEntry,
                                                             true,
                                                             SetIdentityPolicyResultCode::SUCCESS);
    }
    SECTION("Create without data") {
        auto policyEntry =
                setIdentityPolicyTestHelper.createIdentityPolicyEntry(0,
                                                                      accountKey.getPublicKey(),
                                                                      &data);
        // create
        setIdentityPolicyTestHelper.applySetIdentityPolicyTx(account,
                                                             policyEntry,
                                                             false,
                                                             SetIdentityPolicyResultCode::SUCCESS);
        // delete
        policyEntry.id = 1;
        setIdentityPolicyTestHelper.applySetIdentityPolicyTx(account,
                                                             policyEntry,
                                                             true,
                                                             SetIdentityPolicyResultCode::SUCCESS);
    }
}