// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0
#include <transactions/test/test_helper/TestManager.h>
#include <transactions/test/test_helper/ManageAssetTestHelper.h>
#include <transactions/test/test_helper/CreateAccountTestHelper.h>
#include "overlay/LoopbackPeer.h"
#include "main/test.h"
#include "ledger/AccountHelper.h"
#include "ledger/ExternalSystemAccountID.h"
#include "ledger/ExternalSystemAccountIDHelper.h"

using namespace stellar;
using namespace stellar::txtest;

typedef std::unique_ptr<Application> appPtr;

TEST_CASE("create account", "[tx][create_account]") {
    Config const &cfg = getTestConfig(0, Config::TESTDB_POSTGRESQL);

    VirtualClock clock;
    Application::pointer appPtr = Application::create(clock, cfg);
    Application &app = *appPtr;
    app.start();

    upgradeToCurrentLedgerVersion(app);

    LedgerDelta delta(app.getLedgerManager().getCurrentLedgerHeader(),
                      app.getDatabase());


    auto testManager = TestManager::make(app);
    auto root = Account{getRoot(), Salt(1)};

    ManageAssetTestHelper manageAssetHelper(testManager);
    AssetCode baseAsset = "USD";
    manageAssetHelper.createAsset(root, root.key, baseAsset, root,
                                  static_cast<uint32_t>(AssetPolicy::BASE_ASSET));

    auto accountHelper = AccountHelper::Instance();
    auto externalSystemAccountIDHelper = ExternalSystemAccountIDHelper::Instance();

    auto createAccountTestBuilder = CreateAccountTestBuilder();

    auto randomAccount = SecretKey::random();
    createAccountTestBuilder = createAccountTestBuilder
            .setFromAccount(root)
            .setToPublicKey(randomAccount.getPublicKey())
            .setType(AccountType::NOT_VERIFIED);
    auto createAccountHelper = CreateAccountTestHelper(testManager);

    SECTION("External system account id are generated") {

        createAccountHelper.applyTx(createAccountTestBuilder);
        const auto btcKey = externalSystemAccountIDHelper->load(randomAccount.getPublicKey(),
                                                                ExternalSystemType::BITCOIN, app.getDatabase());
        REQUIRE(!!btcKey);
        const auto ethKey = externalSystemAccountIDHelper->load(randomAccount.getPublicKey(),
                                                                ExternalSystemType::ETHEREUM, app.getDatabase());
        REQUIRE(!!ethKey);
        SECTION("Can update account, but ext keys will be the same") {
            createAccountHelper.applyTx(createAccountTestBuilder.setType(AccountType::GENERAL));
            const auto btcKeyAfterUpdate = externalSystemAccountIDHelper->load(randomAccount.getPublicKey(),
                                                                               ExternalSystemType::BITCOIN,
                                                                               app.getDatabase());
            REQUIRE(btcKey->getExternalSystemAccountID() == btcKeyAfterUpdate->getExternalSystemAccountID());
            const auto ethKeyAfterUpdate = externalSystemAccountIDHelper->load(randomAccount.getPublicKey(),
                                                                               ExternalSystemType::ETHEREUM,
                                                                               app.getDatabase());
            REQUIRE(ethKey->getExternalSystemAccountID() == ethKeyAfterUpdate->getExternalSystemAccountID());
        }
    }
    SECTION("Can't create system account") {
        // TODO: change this section to support helpers and builder
        for (auto systemAccountType : getSystemAccountTypes()) {
            auto randomAccount = SecretKey::random();
            auto createAccount = createCreateAccountTx(app.getNetworkID(), root.key, randomAccount, 0,
                                                       systemAccountType);
            applyCheck(createAccount, delta, app);
            REQUIRE(getFirstResult(*createAccount).code() == OperationResultCode::opNOT_ALLOWED);
        }
    }

    SECTION("Can set and update account policies") {
        auto account = SecretKey::random();
        AccountID validReferrer = root.key.getPublicKey();

        auto accountTestBuilder = createAccountTestBuilder
                .setToPublicKey(account.getPublicKey())
                .setReferrer(&validReferrer);

        auto checkAccountPolicies = [&app, &delta](AccountID accountID, LedgerVersion expectedAccountVersion,
                                                   int32 expectedPolicies) {
            auto accountFrame = AccountHelper::Instance()->loadAccount(accountID, app.getDatabase(), &delta);
            REQUIRE(!!accountFrame);
            REQUIRE(accountFrame->getAccount().ext.v() == expectedAccountVersion);
            if (expectedPolicies != -1)
                REQUIRE(accountFrame->getAccount().policies == expectedPolicies);
        };

        SECTION("Can update created without policies") {
            createAccountHelper.applyTx(accountTestBuilder);

            checkAccountPolicies(account.getPublicKey(), LedgerVersion::EMPTY_VERSION, -1);
            // change type of account not_verified -> general
            createAccountHelper.applyTx(accountTestBuilder.setType(AccountType::GENERAL));
            checkAccountPolicies(account.getPublicKey(), LedgerVersion::EMPTY_VERSION,
                                 static_cast<int32_t>(AccountPolicies::NO_PERMISSIONS));

            // can update account's policies no_permissions -> allow_to_create_user_via_api
            createAccountHelper.applyTx(accountTestBuilder.setPolicies(AccountPolicies::ALLOW_TO_CREATE_USER_VIA_API));
            checkAccountPolicies(account.getPublicKey(), LedgerVersion::EMPTY_VERSION,
                                 static_cast<int32_t>(AccountPolicies::ALLOW_TO_CREATE_USER_VIA_API));
            // can remove
            createAccountHelper.applyTx(accountTestBuilder.setPolicies(AccountPolicies::NO_PERMISSIONS));
            checkAccountPolicies(account.getPublicKey(), LedgerVersion::EMPTY_VERSION,
                                 static_cast<int32_t>(AccountPolicies::NO_PERMISSIONS));
        }

        SECTION("Can create account with policies") {
            createAccountHelper.applyTx(accountTestBuilder.setType(AccountType::GENERAL)
                                                .setPolicies(AccountPolicies::ALLOW_TO_CREATE_USER_VIA_API));
            checkAccountPolicies(account.getPublicKey(), LedgerVersion::EMPTY_VERSION,
                                 static_cast<int32_t>(AccountPolicies::ALLOW_TO_CREATE_USER_VIA_API));
        }
    }

    SECTION("Can't create account with non-zero policies and NON_VERYFIED type") {
        auto account = SecretKey::random();
        AccountID validReferrer = root.key.getPublicKey();
        createAccountHelper.applyTx(
                createAccountTestBuilder
                        .setToPublicKey(account.getPublicKey())
                        .setType(AccountType::NOT_VERIFIED)
                        .setReferrer(&validReferrer)
                        .setPolicies(1)
                        .setResultCode(CreateAccountResultCode::TYPE_NOT_ALLOWED)
        );
    }

    SECTION("Root account can create account") {
        auto account = SecretKey::random();

        SECTION("referrer not found") {
            AccountID invalidReferrer = SecretKey::random().getPublicKey();
            createAccountHelper.applyTx(
                    createAccountTestBuilder
                            .setToPublicKey(account.getPublicKey())
                            .setReferrer(&invalidReferrer)
                            .setType(AccountType::GENERAL)
            );
            auto accountFrame = accountHelper->loadAccount(account.getPublicKey(), app.getDatabase());
            REQUIRE(accountFrame);
            REQUIRE(!accountFrame->getReferrer());
        }

        AccountID validReferrer = root.key.getPublicKey();
        auto accountTestBuilder = createAccountTestBuilder
                .setType(AccountType::GENERAL)
                .setReferrer(&validReferrer)
                .setToPublicKey(account.getPublicKey());
        createAccountHelper.applyTx(accountTestBuilder);
        SECTION("Requires med threshold") {
            ThresholdSetter th;
            th.masterWeight = make_optional<uint8_t>(100);
            th.lowThreshold = make_optional<uint8_t>(10);
            th.medThreshold = make_optional<uint8_t>(50);
            th.highThreshold = make_optional<uint8_t>(100);
            auto s1KP = SecretKey::random();
            auto s1 = Signer(s1KP.getPublicKey(), *th.medThreshold.get() - 1,
                             static_cast<int32_t >(SignerType::GENERAL_ACC_MANAGER),
                             1, "", Signer::_ext_t{});
            applySetOptions(app, root.key, root.getNextSalt(), &th, &s1);
            auto createAccount = createAccountHelper.buildTx(accountTestBuilder);
            createAccount->getEnvelope().signatures.clear();
            createAccount->addSignature(s1KP);
            REQUIRE(!applyCheck(createAccount, delta, app));
            REQUIRE(getFirstResultCode(*createAccount) == OperationResultCode::opBAD_AUTH);
        }
        SECTION("Root can create GENERAL account only with account creator signer") {
            auto rootAcc = loadAccount(root.key.getPublicKey(), app);
            auto s1KP = SecretKey::random();
            auto signerTypes = xdr::xdr_traits<SignerType>::enum_values();
            for (auto signerType = signerTypes.begin();
                 signerType != signerTypes.end(); ++signerType) {
                auto s1 = Signer(s1KP.getPublicKey(), rootAcc->getMediumThreshold() + 1,
                                 static_cast<int32_t >(*signerType),
                                 1, "", Signer::_ext_t{});
                applySetOptions(app, root.key, root.getNextSalt(), nullptr, &s1);
                account = SecretKey::random();
                auto createAccount = createAccountHelper.buildTx(
                        accountTestBuilder.setType(AccountType::GENERAL));
                createAccount->getEnvelope().signatures.clear();
                createAccount->addSignature(s1KP);
                auto mustApply = *signerType == static_cast<int32_t >(SignerType::GENERAL_ACC_MANAGER) ||
                                 *signerType == static_cast<int32_t >(SignerType::NOT_VERIFIED_ACC_MANAGER);
                LedgerDelta delta1(app.getLedgerManager().getCurrentLedgerHeader(), app.getDatabase());
                REQUIRE(mustApply == applyCheck(createAccount, delta1, app));
            }
        }
    }
    SECTION("Can change type of account and can change KYC and limits") {
        Limits limits;
        AccountType accountType = AccountType::NOT_VERIFIED;
        limits.dailyOut = 100;
        limits.weeklyOut = 300;
        limits.monthlyOut = 300;
        limits.annualOut = 300;
        applySetLimits(app, root.key, root.getNextSalt(), nullptr, &accountType, limits);

        auto newAccount = SecretKey::random();
        auto accountBuilder = createAccountTestBuilder.setToPublicKey(newAccount.getPublicKey());
        createAccountHelper.applyTx(accountBuilder);
        auto newAccountFrame = accountHelper->loadAccount(newAccount.getPublicKey(), app.getDatabase());

        REQUIRE(newAccountFrame);
        REQUIRE(newAccountFrame->getAccountType() == AccountType::NOT_VERIFIED);

        createAccountHelper.applyTx(accountBuilder.setType(AccountType::GENERAL));
        newAccountFrame = accountHelper->loadAccount(newAccount.getPublicKey(), app.getDatabase());
        REQUIRE(newAccountFrame->getAccountType() == AccountType::GENERAL);

    }
    SECTION("Non root account can't create") {
        for (auto accountType : xdr::xdr_traits<AccountType>::enum_values()) {
            // can be created only once
            if (isSystemAccountType(AccountType(accountType)))
                continue;

            auto accountCreator = SecretKey::random();
            auto notAllowedBuilder = createAccountTestBuilder
                    .setToPublicKey(accountCreator.getPublicKey())
                    .setType(accountType);
            createAccountHelper.applyTx(notAllowedBuilder);
            auto accountCreatorSeq = 1;
            auto notRoot = Account{accountCreator, Salt(1)};
            auto toBeCreated = SecretKey::random();
            auto toBeCreatedHelper = notAllowedBuilder.setToPublicKey(toBeCreated.getPublicKey())
                    .setFromAccount(notRoot)
                    .setType(AccountType::GENERAL);

            testManager->applyAndCheckFirstOperation(
                    createAccountHelper.buildTx(toBeCreatedHelper),
                    OperationResultCode::opNOT_ALLOWED
            );
        }
    }
    SECTION("Can update not verified to syndicate") {
        auto toBeCreated = SecretKey::random();
        auto toBeSyndicateBuilder = createAccountTestBuilder.setToPublicKey(toBeCreated.getPublicKey())
                .setType(AccountType::NOT_VERIFIED);
        createAccountHelper.applyTx(toBeSyndicateBuilder);
        createAccountHelper.applyTx(toBeSyndicateBuilder.setType(AccountType::SYNDICATE));
    }
    for (auto accountType : getAllAccountTypes()) {
        // can be created only once
        if (isSystemAccountType(AccountType(accountType)))
            continue;

        for (auto updateAccountType : getAllAccountTypes()) {
            if (isSystemAccountType(AccountType(updateAccountType)))
                continue;
            if (updateAccountType == accountType)
                continue;
            const auto isAllowedToUpdateTo =
                    updateAccountType == AccountType::GENERAL || updateAccountType == AccountType::SYNDICATE;
            if (isAllowedToUpdateTo && accountType == AccountType::NOT_VERIFIED)
                continue;
            auto toBeCreated = SecretKey::random();
            auto toBeCreatedBuilder = createAccountTestBuilder
                    .setToPublicKey(toBeCreated.getPublicKey())
                    .setType(accountType);

            createAccountHelper.applyTx(toBeCreatedBuilder);
            createAccountHelper.applyTx(toBeCreatedBuilder
                                                .setType(updateAccountType)
                                                .setResultCode(CreateAccountResultCode::TYPE_NOT_ALLOWED)
            );
        }
    }
}
