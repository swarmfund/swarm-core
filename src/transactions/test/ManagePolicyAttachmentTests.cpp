#include "TxTests.h"
#include "crypto/SHA.h"
#include "ledger/AccountHelper.h"
#include "ledger/LedgerDelta.h"
#include "main/Application.h"
#include "main/test.h"
#include "overlay/LoopbackPeer.h"
#include "test/test_marshaler.h"
#include "test_helper/CreateAccountTestHelper.h"
#include "transactions/test/test_helper/SetAccountRoleTestHelper.h"
#include "transactions/test/test_helper/SetAccountRolePolicyTestHelper.h"
#include "transactions/BindExternalSystemAccountIdOpFrame.h"
#include "transactions/SetAccountRolePolicyOpFrame.h"
#include "util/make_unique.h"

using namespace stellar;
using namespace stellar::txtest;

typedef std::unique_ptr<Application> appPtr;

TEST_CASE("Manage policy attachment", "[tx][manage_policy_attachment]")
{/*
    Config const& cfg = getTestConfig(0, Config::TESTDB_POSTGRESQL);

    VirtualClock clock;
    Application::pointer appPtr = Application::create(clock, cfg);
    Application& app = *appPtr;
    app.start();
    TestManager::upgradeToCurrentLedgerVersion(app);

    Database& db = app.getDatabase();

    auto testManager = TestManager::make(app);

    LedgerDelta delta(app.getLedgerManager().getCurrentLedgerHeader(),
                      app.getDatabase());

    // set up world
    auto master = Account{getRoot(), Salt(1)};

    CreateAccountTestHelper createAccountTestHelper(testManager);
    SetAccountRoleTestHelper managePAHelper(testManager);
    SetAccountRolePolicyTestHelper setIdentityPolicyHelper(testManager);

    // create account for further tests
    auto accountKey = SecretKey::random();
    auto account = Account{accountKey, Salt(1)};

    createAccountTestHelper.applyCreateAccountTx(
        master, accountKey.getPublicKey(), AccountType::GENERAL);

    // create policy
    auto data = SetIdentityPolicyData(
        PRIORITY_USER_MIN, "resource_type:::", "SomeAction", Effect::DENY,
        SetIdentityPolicyData::_ext_t{});

    auto policyEntry = setIdentityPolicyHelper.createIdentityPolicyEntry(
        0, accountKey.getPublicKey(), &data);
    setIdentityPolicyHelper.applySetIdentityPolicyTx(
        account, policyEntry, false, SetIdentityPolicyResultCode::SUCCESS);

    SECTION("Identity policy not found")
    {
        auto actorWithAccID =
            managePAHelper.createActorForAccountID(account.key.getPublicKey());
        // not-existent identity policy
        auto creationOpInput =
            managePAHelper.createCreationOpInput(2, actorWithAccID);

        managePAHelper.applyManagePolicyAttachment(
            account, creationOpInput,
            ManagePolicyAttachmentResultCode::POLICY_NOT_FOUND);
    }
    SECTION("Successful creation")
    {
        {
            // create for account id
            auto actorWithAccID = managePAHelper.createActorForAccountID(
                account.key.getPublicKey());
            auto creationOpInput =
                managePAHelper.createCreationOpInput(1, actorWithAccID);

            managePAHelper.applyManagePolicyAttachment(account,
                                                       creationOpInput);
        }
        {
            // create for account type
            auto actorWithAccType =
                managePAHelper.createActorForAccountType(AccountType::GENERAL);
            auto creationOpInput =
                managePAHelper.createCreationOpInput(1, actorWithAccType);

            managePAHelper.applyManagePolicyAttachment(account,
                                                       creationOpInput);
        }
        {
            // create for any actor
            auto actorAny = managePAHelper.createActorForAnyAccount();
            auto creationOpInput =
                managePAHelper.createCreationOpInput(1, actorAny);

            managePAHelper.applyManagePolicyAttachment(account,
                                                       creationOpInput);
        }
    }
    SECTION("Deleting non-existent policy attachment")
    {
        auto creationOpInput = managePAHelper.createDeletionOpInput(2);

        managePAHelper.applyManagePolicyAttachment(
            account, creationOpInput,
            ManagePolicyAttachmentResultCode::POLICY_ATTACHMENT_NOT_FOUND);
    }
    SECTION("Deleting policy attachment")
    {
        auto actorWithAccType =
            managePAHelper.createActorForAccountType(AccountType::GENERAL);
        auto creationOpInput =
            managePAHelper.createCreationOpInput(1, actorWithAccType);
        // trying to delete policy attachment
        managePAHelper.applyManagePolicyAttachment(account, creationOpInput);
    }
    SECTION("Creating identical policy attachment")
    {
        auto actorWithAccID =
            managePAHelper.createActorForAccountID(account.key.getPublicKey());
        auto creationOpInput =
            managePAHelper.createCreationOpInput(1, actorWithAccID);
        // creating policy attachment
        managePAHelper.applyManagePolicyAttachment(account, creationOpInput);
        // trying to create duplicated policy attachment
        managePAHelper.applyManagePolicyAttachment(
            account, creationOpInput,
            ManagePolicyAttachmentResultCode::ATTACHMENT_ALREADY_EXISTS);
    }
    SECTION("Creating policy attachment for non-existent account")
    {
        auto actorWithAccID = managePAHelper.createActorForAccountID(
            SecretKey::random().getPublicKey());
        auto creationOpInput =
            managePAHelper.createCreationOpInput(1, actorWithAccID);

        managePAHelper.applyManagePolicyAttachment(
            account, creationOpInput,
            ManagePolicyAttachmentResultCode::DESTINATION_ACCOUNT_NOT_FOUND);
    }
    SECTION("Too many policy attachments per account")
    {
        // create first policy attachment with identity policy created before
        // section
        auto actorWithAccType =
            managePAHelper.createActorForAccountType(AccountType::GENERAL);
        {
            auto creationOpInput =
                managePAHelper.createCreationOpInput(1, actorWithAccType);
            managePAHelper.applyManagePolicyAttachment(account,
                                                       creationOpInput);
        }
        // fill in all policy attachments
        int i = 2;
        for (; i <= 100; ++i)
        {
            auto policyEntry =
                setIdentityPolicyHelper.createIdentityPolicyEntry(
                    0, accountKey.getPublicKey(), &data);
            setIdentityPolicyHelper.applySetIdentityPolicyTx(
                account, policyEntry, false,
                SetIdentityPolicyResultCode::SUCCESS);

            auto creationOpInput =
                managePAHelper.createCreationOpInput(i, actorWithAccType);

            managePAHelper.applyManagePolicyAttachment(account,
                                                       creationOpInput);
        }
        // trying to create one more policy attachment with new identity policy
        auto actorWithAnyAccount = managePAHelper.createActorForAnyAccount();
        auto creationOpInput =
            managePAHelper.createCreationOpInput(i - 1, actorWithAnyAccount);

        managePAHelper.applyManagePolicyAttachment(
            account, creationOpInput,
            ManagePolicyAttachmentResultCode::
                POLICY_ATTACHMENTS_LIMIT_EXCEEDED);
    }*/
}