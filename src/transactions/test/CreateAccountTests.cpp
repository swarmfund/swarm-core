// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0
#include <transactions/test/test_helper/TestManager.h>
#include <transactions/test/test_helper/ManageAssetTestHelper.h>
#include <transactions/test/test_helper/CreateAccountTestHelper.h>
#include <exsysidgen/Generator.h>
#include "overlay/LoopbackPeer.h"
#include "main/test.h"
#include "ledger/AccountHelper.h"
#include "ledger/ExternalSystemAccountID.h"
#include "ledger/ExternalSystemAccountIDHelperLegacy.h"
#include "test/test_marshaler.h"

using namespace stellar;
using namespace stellar::txtest;

typedef std::unique_ptr<Application> appPtr;

TEST_CASE("create account", "[tx][create_account]") {
    Config const &cfg = getTestConfig(0, Config::TESTDB_POSTGRESQL);

    VirtualClock clock;
    Application::pointer appPtr = Application::create(clock, cfg);
    Application &app = *appPtr;
    app.start();
    TestManager::upgradeToCurrentLedgerVersion(app);

    auto testManager = TestManager::make(app);
    auto root = Account{getRoot(), Salt(1)};

    auto randomAccount = SecretKey::random();
    auto createAccountTestBuilder = CreateAccountTestBuilder()
            .setSource(root)
            .setToPublicKey(randomAccount.getPublicKey())
            .setType(AccountType::NOT_VERIFIED)
            .setRecovery(SecretKey::random().getPublicKey());

    auto createAccountHelper = CreateAccountTestHelper(testManager);

    SECTION("External system account id are generated") {
        auto externalSystemAccountIDHelper = ExternalSystemAccountIDHelperLegacy::Instance();
        createAccountHelper.applyTx(createAccountTestBuilder);
        const auto btcKey = externalSystemAccountIDHelper->load(randomAccount.getPublicKey(),
                                                                BitcoinExternalSystemType, app.getDatabase());
        REQUIRE(!!btcKey);

        const auto ethKey = externalSystemAccountIDHelper->load(randomAccount.getPublicKey(),
                                                                EthereumExternalSystemType, app.getDatabase());
        REQUIRE(!!ethKey);

        SECTION("Can update account, but ext keys will be the same") {
            createAccountHelper.applyTx(createAccountTestBuilder.setType(AccountType::GENERAL));
            const auto btcKeyAfterUpdate = externalSystemAccountIDHelper->load(randomAccount.getPublicKey(),
                                                                               BitcoinExternalSystemType,
                                                                               app.getDatabase());
            REQUIRE(btcKey->getExternalSystemAccountID() == btcKeyAfterUpdate->getExternalSystemAccountID());
            const auto ethKeyAfterUpdate = externalSystemAccountIDHelper->load(randomAccount.getPublicKey(),
                                                                               EthereumExternalSystemType,
                                                                               app.getDatabase());
            REQUIRE(ethKey->getExternalSystemAccountID() == ethKeyAfterUpdate->getExternalSystemAccountID());
        }
    }
    SECTION("Can't create system account") {
        auto systemCreateAccountBuilder =
                createAccountTestBuilder.setOperationResultCode(OperationResultCode::opNOT_ALLOWED);
        for (auto systemAccountType : getSystemAccountTypes()) {
            auto randomAccount = SecretKey::random();
            systemCreateAccountBuilder =
                    systemCreateAccountBuilder.setType(systemAccountType).setToPublicKey(randomAccount.getPublicKey());
            createAccountHelper.applyTx(systemCreateAccountBuilder);
        }
    }

    SECTION("Can set and update account policies") {
        auto account = SecretKey::random();
        AccountID validReferrer = root.key.getPublicKey();

        auto accountTestBuilder = createAccountTestBuilder
                .setToPublicKey(account.getPublicKey())
                .setReferrer(&validReferrer);

        SECTION("Can update created without policies") {
            createAccountHelper.applyTx(accountTestBuilder);
            // change type of account not_verified -> general
            createAccountHelper.applyTx(accountTestBuilder.setType(AccountType::GENERAL));
            // can update account's policies no_permissions -> allow_to_create_user_via_api
            createAccountHelper.applyTx(accountTestBuilder.setType(AccountType::GENERAL)
                                                .setPolicies(AccountPolicies::ALLOW_TO_CREATE_USER_VIA_API));
            // can remove
            createAccountHelper.applyTx(accountTestBuilder.setType(AccountType::GENERAL)
                                                .setPolicies(AccountPolicies::NO_PERMISSIONS));
        }

        SECTION("Can create account with policies") {
            createAccountHelper.applyTx(accountTestBuilder.setType(AccountType::GENERAL)
                                                .setPolicies(AccountPolicies::ALLOW_TO_CREATE_USER_VIA_API));
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
                        .setResultCode(CreateAccountResultCode::NOT_VERIFIED_CANNOT_HAVE_POLICIES)
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
            auto accountFrame = AccountHelper::Instance()->loadAccount(account.getPublicKey(), app.getDatabase());
            REQUIRE(accountFrame);
            REQUIRE(!accountFrame->getReferrer());
        }

        AccountID validReferrer = root.key.getPublicKey();
        auto accountTestBuilder = createAccountTestBuilder
                .setType(AccountType::GENERAL)
                .setReferrer(&validReferrer)
                .setToPublicKey(account.getPublicKey());
        createAccountHelper.applyTx(accountTestBuilder);

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
                auto createAccount = accountTestBuilder.setType(AccountType::GENERAL).buildTx(testManager);
                createAccount->getEnvelope().signatures.clear();
                createAccount->addSignature(s1KP);
                auto mustApply = *signerType == static_cast<int32_t >(SignerType::GENERAL_ACC_MANAGER) ||
                                 *signerType == static_cast<int32_t >(SignerType::NOT_VERIFIED_ACC_MANAGER);
                REQUIRE(mustApply == testManager->applyCheck(createAccount));
            }
        }
    }

    SECTION("Non root account can't create") {
        for (auto accountType : xdr::xdr_traits<AccountType>::enum_values()) {
            // can be created only once
            if (isSystemAccountType(AccountType(accountType)) ||
                accountType == static_cast<int32_t >(AccountType::ACCREDITED_INVESTOR) ||
                accountType == static_cast<int32_t >(AccountType::INSTITUTIONAL_INVESTOR))
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
                    .setSource(notRoot)
                    .setType(AccountType::GENERAL)
                    .setOperationResultCode(OperationResultCode::opNOT_ALLOWED);
            createAccountHelper.applyTx(toBeCreatedHelper);
        }
    }
    SECTION("Can update not verified to syndicate") {
        auto toBeCreated = SecretKey::random();
        auto toBeSyndicateBuilder = createAccountTestBuilder.setToPublicKey(toBeCreated.getPublicKey())
                .setType(AccountType::NOT_VERIFIED);
        createAccountHelper.applyTx(toBeSyndicateBuilder);
        createAccountHelper.applyTx(toBeSyndicateBuilder.setType(AccountType::SYNDICATE));
    }
    SECTION("Can only change account type from Not verified to general") {
        auto pairs = combineElements<AccountType>(
                {AccountType::GENERAL,
                 AccountType::SYNDICATE,
                 AccountType::EXCHANGE},
                {AccountType::NOT_VERIFIED}
        );

        for (auto pair : pairs) {
            auto toBeCreated = SecretKey::random();
            auto toBeCreatedBuilder = createAccountTestBuilder
                    .setToPublicKey(toBeCreated.getPublicKey())
                    .setType(pair.first);
            createAccountHelper.applyTx(toBeCreatedBuilder);
            createAccountHelper.applyTx(toBeCreatedBuilder.setType(pair.second)
                                                .setResultCode(CreateAccountResultCode::TYPE_NOT_ALLOWED));
        }
    }
}
