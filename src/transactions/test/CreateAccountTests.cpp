// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0
#include <transactions/test/test_helper/TestManager.h>
#include <transactions/test/test_helper/ManageAssetTestHelper.h>
#include <transactions/test/test_helper/CreateAccountTestHelper.h>
#include "main/Application.h"
#include "util/Timer.h"
#include "main/Config.h"
#include "overlay/LoopbackPeer.h"
#include "main/test.h"
#include "lib/catch.hpp"
#include "TxTests.h"
#include "ledger/LedgerManager.h"
#include "ledger/LedgerDelta.h"
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

    auto testManager = TestManager::make(app);
    auto root = Account{getRoot(), Salt(0)};

    auto accountKP = SecretKey::random();
    auto createAccountTestHelper =
            CreateAccountTestHelper(testManager)
                    .setType(AccountType::GENERAL)
                    .setFromAccount(root)
                    .setToPublicKey(accountKP.getPublicKey());

    upgradeToCurrentLedgerVersion(app);

    LedgerDelta delta(app.getLedgerManager().getCurrentLedgerHeader(),
                      app.getDatabase());

    ManageAssetTestHelper manageAssetHelper(testManager);
    AssetCode baseAsset = "USD";
    manageAssetHelper.createAsset(root, root.key, baseAsset, root,
                                  static_cast<uint32_t>(AssetPolicy::BASE_ASSET));

    auto accountHelper = AccountHelper::Instance();
    auto externalSystemAccountIDHelper = ExternalSystemAccountIDHelper::Instance();

    auto randomAccount = SecretKey::random();
    auto createAccountHelper = createAccountTestHelper.setToPublicKey(randomAccount.getPublicKey())
            .setType(AccountType::NOT_VERIFIED);

    SECTION("External system account id are generated") {

        createAccountHelper.applyTx();
        const auto btcKey = externalSystemAccountIDHelper->load(randomAccount.getPublicKey(),
                                                                ExternalSystemType::BITCOIN, app.getDatabase());
        REQUIRE(!!btcKey);
        const auto ethKey = externalSystemAccountIDHelper->load(randomAccount.getPublicKey(),
                                                                ExternalSystemType::ETHEREUM, app.getDatabase());
        REQUIRE(!!ethKey);
        SECTION("Can update account, but ext keys will be the same") {
            createAccountHelper.setType(AccountType::GENERAL).applyTx();
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
        auto checkAccountPolicies = [&app, &delta](AccountID accountID, LedgerVersion expectedAccountVersion,
                                                   int32 expectedPolicies) {
            auto accountFrame = AccountHelper::Instance()->loadAccount(accountID, app.getDatabase(), &delta);
            REQUIRE(accountFrame);
            REQUIRE(accountFrame->getAccount().ext.v() == expectedAccountVersion);
            if (expectedPolicies != -1)
                REQUIRE(accountFrame->getAccount().policies == expectedPolicies);
        };

        SECTION("Can update created without policies") {
            auto account = SecretKey::random();
            auto withoutPoliciesAccHelper = createAccountHelper.setToPublicKey(account.getPublicKey())
                    .setReferrer(&validReferrer);
            withoutPoliciesAccHelper.applyTx();


            checkAccountPolicies(account.getPublicKey(), LedgerVersion::EMPTY_VERSION, -1);
            // change type of account not_verified -> general
            withoutPoliciesAccHelper = withoutPoliciesAccHelper.setType(AccountType::GENERAL);
            withoutPoliciesAccHelper.applyTx();
            checkAccountPolicies(account.getPublicKey(), LedgerVersion::EMPTY_VERSION,
                                 static_cast<int32_t>(AccountPolicies::NO_PERMISSIONS));

            // can update account's policies no_permissions -> allow_to_create_user_via_api
            withoutPoliciesAccHelper.setPolicies(AccountPolicies::ALLOW_TO_CREATE_USER_VIA_API).applyTx();
            checkAccountPolicies(account.getPublicKey(), LedgerVersion::EMPTY_VERSION,
                                 static_cast<int32_t>(AccountPolicies::ALLOW_TO_CREATE_USER_VIA_API));
            // can remove
            withoutPoliciesAccHelper.setPolicies(AccountPolicies::NO_PERMISSIONS).applyTx();

            checkAccountPolicies(account.getPublicKey(), LedgerVersion::EMPTY_VERSION,
                                 static_cast<int32_t>(AccountPolicies::NO_PERMISSIONS));
        }

        SECTION("Can create account with policies") {
            createAccountHelper.setType(AccountType::GENERAL)
                    .setPolicies(AccountPolicies::ALLOW_TO_CREATE_USER_VIA_API).applyTx();
            checkAccountPolicies(account.getPublicKey(), LedgerVersion::EMPTY_VERSION,
                                 static_cast<int32_t>(AccountPolicies::ALLOW_TO_CREATE_USER_VIA_API));
        }
    }

    SECTION("Can't create account with non-zero policies and NON_VERYFIED type") {
        auto account = SecretKey::random();
        AccountID validReferrer = root.key.getPublicKey();
        createAccountHelper.setToPublicKey(account.getPublicKey())
                .setReferrer(&validReferrer)
                .setResultCode(CreateAccountResultCode::TYPE_NOT_ALLOWED)
                .applyTx();
    }

    SECTION("Root account can create account") {
        auto account = SecretKey::random();

        SECTION("referrer not found") {
            AccountID invalidReferrer = SecretKey::random().getPublicKey();
            createAccountHelper.setToPublicKey(account.getPublicKey())
                    .setReferrer(&invalidReferrer)
                    .setType(AccountType::GENERAL)
                    .applyTx();
            auto accountFrame = accountHelper->loadAccount(account.getPublicKey(), app.getDatabase());
            REQUIRE(accountFrame);
            REQUIRE(!accountFrame->getReferrer());
        }

        AccountID validReferrer = root.key.getPublicKey();
        auto createAccountTestHelper = createAccountHelper.setType(AccountType::GENERAL)
                .setReferrer(&validReferrer)
                .setToPublicKey(account.getPublicKey());
        createAccountTestHelper.applyTx();
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
            auto createAccount = createAccountTestHelper.createCreateAccountTx();
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
                auto createAccount = createAccountTestHelper.setType(AccountType::GENERAL).createCreateAccountTx();
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
        auto accountTestHelper = createAccountHelper.setToPublicKey(newAccount.getPublicKey());
        accountTestHelper.applyTx();
        auto newAccountFrame = accountHelper->loadAccount(newAccount.getPublicKey(), app.getDatabase());

        REQUIRE(newAccountFrame);
        REQUIRE(newAccountFrame->getAccountType() == AccountType::NOT_VERIFIED);

        accountTestHelper.setType(AccountType::GENERAL).applyTx();
        newAccountFrame = accountHelper->loadAccount(newAccount.getPublicKey(), app.getDatabase());
        REQUIRE(newAccountFrame->getAccountType() == AccountType::GENERAL);

    }
    SECTION("Non root account can't create") {
        for (auto accountType : xdr::xdr_traits<AccountType>::enum_values()) {
            // can be created only once
            if (isSystemAccountType(AccountType(accountType)))
                continue;

            auto accountCreator = SecretKey::random();
            createAccountHelper.setToPublicKey(accountCreator.getPublicKey()).setType(accountType).applyTx();
            auto accountCreatorSeq = 1;
            auto toBeCreated = SecretKey::random();
            auto toBeCreatedHelper = createAccountHelper.setToPublicKey(accountCreator.getPublicKey())
                    .setType(AccountType::GENERAL);
            auto createAccount = toBeCreatedHelper.createCreateAccountTx();
            applyCheck(createAccount, delta, app);
            REQUIRE(getFirstResult(*createAccount).code() == OperationResultCode::opNOT_ALLOWED);
        }
    }
    SECTION("Can update not verified to syndicate") {
        auto toBeCreated = SecretKey::random();
        auto toBeSyndicateHelper = createAccountHelper.setToPublicKey(toBeCreated.getPublicKey())
                .setType(AccountType::NOT_VERIFIED);
        toBeSyndicateHelper.applyTx();
        toBeSyndicateHelper.setType(AccountType::SYNDICATE).applyTx();
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
            createAccountHelper.setToPublicKey(toBeCreated.getPublicKey()).setType(accountType).applyTx();
            createAccountHelper.setToPublicKey(toBeCreated.getPublicKey())
                    .setType(updateAccountType)
                    .setResultCode(CreateAccountResultCode::TYPE_NOT_ALLOWED)
                    .applyTx();
        }
    }
}
