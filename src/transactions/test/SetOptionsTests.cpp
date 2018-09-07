// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "main/Application.h"
#include "main/Config.h"
#include "util/Timer.h"
#include "overlay/LoopbackPeer.h"
#include "util/make_unique.h"
#include "main/test.h"
#include "TxTests.h"
#include "crypto/SHA.h"
#include "ledger/LedgerDeltaImpl.h"
#include "ledger/AccountHelper.h"
#include "ledger/TrustFrame.h"
#include "ledger/TrustHelper.h"
#include "test_helper/CreateAccountTestHelper.h"
#include "test_helper/ManageAssetTestHelper.h"
#include "test_helper/ManageBalanceTestHelper.h"
#include "test_helper/SetOptionsTestHelper.h"
#include "test/test_marshaler.h"

using namespace stellar;
using namespace stellar::txtest;

typedef std::unique_ptr<Application> appPtr;

// Try setting each option to make sure it works
// try setting all at once
// try setting high threshold ones without the correct sigs
// make sure it doesn't allow us to add signers when we don't have the
// minbalance
TEST_CASE("set options", "[tx][set_options]")
{
    using xdr::operator==;

    Config const& cfg = getTestConfig(0, Config::TESTDB_POSTGRESQL);

    VirtualClock clock;
    Application::pointer appPtr = Application::create(clock, cfg);
    Application& app = *appPtr;
    app.start();
    closeLedgerOn(app, 2, 1, 7, 2014);

    auto testManager = TestManager::make(app);

	upgradeToCurrentLedgerVersion(app);

    // set up world
    auto root = Account{ getRoot(), Salt(0) };
    auto a1 = Account { getAccount("A"), Salt(0) };

    CreateAccountTestHelper createAccountTestHelper(testManager);
    createAccountTestHelper.applyCreateAccountTx(root, a1.key.getPublicKey(), AccountType::GENERAL);

	auto accountHelper = AccountHelper::Instance();
	auto trustHelper = TrustHelper::Instance();

    SetOptionsTestHelper setOptionsTestHelper(testManager);

    SECTION("Signers")
    {
        SecretKey s1 = getAccount("S1");
        Signer sk1(s1.getPublicKey(), 1, getAnySignerType() & ~static_cast<int32_t>(SignerType::ACCOUNT_MANAGER),
				   1, "", Signer::_ext_t{}); // low right account

        ThresholdSetter th;

        th.masterWeight = make_optional<uint8_t>(100);
        th.lowThreshold = make_optional<uint8_t>(1);
        th.medThreshold = make_optional<uint8_t>(10);
        th.highThreshold = make_optional<uint8_t>(100);

		SECTION("Can't use non account manager signer for master weight OR threshold or signer")
		{
            setOptionsTestHelper.applySetOptionsTx(a1, &th, nullptr);

			SecretKey regularKP = getAccount("regular");
			auto a1Account = loadAccount(a1.key, app);
			Signer regular(regularKP.getPublicKey(), a1Account->getHighThreshold(),
						   getAnySignerType() & ~static_cast<int32_t>(SignerType::ACCOUNT_MANAGER), 2, "", Signer::_ext_t{}); // high right regular account
            setOptionsTestHelper.applySetOptionsTx(a1, nullptr, &regular);

			LedgerDeltaImpl delta(app.getLedgerManager().getCurrentLedgerHeader(),
				app.getDatabase());

			SECTION("Can't add new signer")
			{
				SecretKey s2KP = getAccount("s2");
				Signer s2(s2KP.getPublicKey(), a1Account->getHighThreshold(),
						  getAnySignerType() & ~static_cast<int32_t>(SignerType::ACCOUNT_MANAGER), 2, "", Signer::_ext_t{}); // high right regular account
				auto tx = setOptionsTestHelper.createSetOptionsTx(a1, nullptr, &s2);
				tx->getEnvelope().signatures.clear();
				tx->addSignature(regularKP);
                testManager->applyCheck(tx);
				REQUIRE(getFirstResult(*tx).code() == OperationResultCode::opNOT_ALLOWED);
			}
			SECTION("Can't change threshold")
			{
				auto tx = setOptionsTestHelper.createSetOptionsTx(a1, &th, nullptr);
				tx->getEnvelope().signatures.clear();
				tx->addSignature(regularKP);
				testManager->applyCheck(tx);
				REQUIRE(getFirstResult(*tx).code() == OperationResultCode::opNOT_ALLOWED);
			}

		}
        SECTION("can't use master key as alternate signer")
        {
            Signer sk(a1.key.getPublicKey(), 100, getAnySignerType() & ~static_cast<int32_t>(SignerType::ACCOUNT_MANAGER),
					  0, "", Signer::_ext_t{});
            setOptionsTestHelper.applySetOptionsTx(a1, nullptr, &sk, nullptr, nullptr, SetOptionsResultCode::BAD_SIGNER);
        }

		LedgerDeltaImpl delta(app.getLedgerManager().getCurrentLedgerHeader(),
			app.getDatabase());
		auto checkSignerName = [&app, &delta](AccountID accountID, Signer expectedSigner) {
			auto accountHelper = AccountHelper::Instance();
			auto account = accountHelper->loadAccount(accountID, app.getDatabase(), &delta);
			for (auto signer : account->getAccount().signers)
			{
				if (signer.pubKey == expectedSigner.pubKey)
				{
					REQUIRE(signer == expectedSigner);
					return;
				}
			}

			// failed to find signer
			REQUIRE(false);
		};

		SECTION("Can set and update signer name")
		{
			SecretKey regularKP = getAccount("regular");
			auto a1Account = loadAccount(a1.key, app);
			Signer regular(regularKP.getPublicKey(), 10, getAnySignerType(), 2, "", Signer::_ext_t{}); // high right regular account

			std::string name = "Test signer name";
			regular.name = name;
            setOptionsTestHelper.applySetOptionsTx(a1, nullptr, &regular);
			checkSignerName(a1.key.getPublicKey(), regular);

			//update signer name
			name += "New";
			regular.name = name;
            setOptionsTestHelper.applySetOptionsTx(a1, nullptr, &regular);
			checkSignerName(a1.key.getPublicKey(), regular);

		}
        SECTION("can not add Trust with same accountID")
        {
            TrustData trustData;
            TrustEntry trust;
            trust.allowedAccount = a1.key.getPublicKey();
            trust.balanceToUse = a1.key.getPublicKey();
            trustData.trust = trust;
            setOptionsTestHelper.applySetOptionsTx(a1, nullptr, nullptr, &trustData, nullptr,
                                                   SetOptionsResultCode::TRUST_MALFORMED);
        }

        SECTION("can not add Trust if no balance")
        {
            auto newAccount = Account{ SecretKey::random(), Salt(0) };
            AccountID newAccountID = newAccount.key.getPublicKey();
            createAccountTestHelper.applyCreateAccountTx(root, newAccountID, AccountType::GENERAL);

            TrustData trustData;
            TrustEntry trust;
            trust.allowedAccount = newAccountID;
            trust.balanceToUse = SecretKey::random().getPublicKey();
            trustData.trust = trust;

            setOptionsTestHelper.applySetOptionsTx(a1, nullptr, nullptr, &trustData, nullptr,
            SetOptionsResultCode::BALANCE_NOT_FOUND);
        }
        SECTION("can not add Trust if balance from wrong account")
        {
            auto newAccount = Account{ SecretKey::random(), Salt(0) };
            AccountID newAccountID = newAccount.key.getPublicKey();
            createAccountTestHelper.applyCreateAccountTx(root, newAccountID, AccountType::GENERAL);

            TrustData trustData;
            TrustEntry trust;
            trust.allowedAccount = newAccountID;
            trust.balanceToUse = root.key.getPublicKey();
            trustData.trust = trust;

            setOptionsTestHelper.applySetOptionsTx(a1, nullptr, nullptr, &trustData, nullptr,
                                                   SetOptionsResultCode::BALANCE_NOT_FOUND);
        }
        SECTION("can add Trust")
        {
            auto trustAccount = Account{ SecretKey::random(), Salt(0) };
            REQUIRE(!trustHelper->exists(testManager->getDB(),
                trustAccount.key.getPublicKey(), a1.key.getPublicKey()));

            createAccountTestHelper.applyCreateAccountTx(root, trustAccount.key.getPublicKey(),
                                                         AccountType::GENERAL);

            AssetCode asset = "EUR";
            auto manageAssetHelper = ManageAssetTestHelper(testManager);
            auto preissuedSigner = SecretKey::random();
            manageAssetHelper.createAsset(root, preissuedSigner, asset, root, 1);

            ManageBalanceTestHelper manageBalanceTestHelper(testManager);
            AccountID a1ID = a1.key.getPublicKey();
            auto manageBalanceResult = manageBalanceTestHelper.applyManageBalanceTx(a1, a1ID, asset);

            TrustData trustData;
            TrustEntry trust;
            trust.allowedAccount = trustAccount.key.getPublicKey();
            trust.balanceToUse = manageBalanceResult.success().balanceID;
            trustData.trust = trust;
            trustData.action = ManageTrustAction::TRUST_ADD;
            setOptionsTestHelper.applySetOptionsTx(a1, nullptr, nullptr, &trustData);
            
            REQUIRE(trustHelper->exists(testManager->getDB(),
                trustAccount.key.getPublicKey(), manageBalanceResult.success().balanceID));

            SECTION("can delete")
            {
                trustData.action = ManageTrustAction::TRUST_REMOVE;
                setOptionsTestHelper.applySetOptionsTx(a1, nullptr, nullptr, &trustData);

                REQUIRE(!trustHelper->exists(testManager->getDB(),
                    trustAccount.key.getPublicKey(), manageBalanceResult.success().balanceID));
            }
        }


        SECTION("multiple signers")
        {
			auto checkSigner = [](Application& app, Signer expectedSigner, int expectedSignersSize, SecretKey accountKP) {
				AccountFrame::pointer account = loadAccount(accountKP, app);
				bool found = false;
				auto signers = account->getAccount().signers;
				REQUIRE(signers.size() == expectedSignersSize);
				for (auto it = signers.begin(); it != signers.end(); it++)
				{
					if (it->pubKey == expectedSigner.pubKey)
					{
						found = true;
						REQUIRE(it->identity == expectedSigner.identity);
						REQUIRE(it->signerType == expectedSigner.signerType);
						REQUIRE(it->weight == expectedSigner.weight);
						break;
					}
				}
				REQUIRE(found);
			};

            setOptionsTestHelper.applySetOptionsTx(a1, &th, &sk1);
			// add signer
			checkSigner(app, sk1, 1, a1.key);

			// update weight
			sk1.weight = sk1.weight + 1;
            setOptionsTestHelper.applySetOptionsTx(a1, &th, &sk1);
			checkSigner(app, sk1, 1, a1.key);

			// update type
			sk1.signerType = static_cast<int32_t>(SignerType::ACCOUNT_MANAGER);
            setOptionsTestHelper.applySetOptionsTx(a1, &th, &sk1);
			checkSigner(app, sk1, 1, a1.key);

			// update identity
			sk1.identity = sk1.identity + 1;
            setOptionsTestHelper.applySetOptionsTx(a1, &th, &sk1);
			checkSigner(app, sk1, 1, a1.key);


            // add signer 2
            auto s2 = Account{ getAccount("S2"), Salt(0) };
            Signer sk2(s2.key.getPublicKey(), 100, getAnySignerType(), 2, "", Signer::_ext_t{});
            setOptionsTestHelper.applySetOptionsTx(a1, nullptr, &sk2);

			checkSigner(app, sk1, 2, a1.key);
			checkSigner(app, sk2, 2, a1.key);

			// remove signer 1
            sk1.weight = 0;
            setOptionsTestHelper.applySetOptionsTx(a1, nullptr, &sk1);
			checkSigner(app, sk2, 1, a1.key);

            // remove signer 2
            sk2.weight = 0;
            setOptionsTestHelper.applySetOptionsTx(a1, nullptr, &sk2);

            auto a1Account = loadAccount(a1.key, app);
            REQUIRE(a1Account->getAccount().signers.size() == 0);
        }
    }
/*
    SECTION("Limits update request")
    {
        // create requestor
        auto requestor = Account{ SecretKey::random(), Salt(0) };
        AccountID requestorID = requestor.key.getPublicKey();
        createAccountTestHelper.applyCreateAccountTx(root, requestorID,
                                                     AccountType::GENERAL);

        // prepare data for request
        std::string documentData = "Some document data";
        stellar::Hash documentHash = Hash(sha256(documentData));

        LimitsUpdateRequestData limitsUpdateRequestData;
        limitsUpdateRequestData.documentHash = documentHash;

        setOptionsTestHelper.applySetOptionsTx(requestor, nullptr, nullptr, nullptr, &limitsUpdateRequestData);
    }
*/
    // these are all tested by other tests
    // set transfer rate
    // set data
    // set thresholds
    // set signer
}
