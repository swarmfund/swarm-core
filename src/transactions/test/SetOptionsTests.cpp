// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include <transactions/test/test_helper/TestManager.h>
#include <transactions/test/test_helper/Account.h>
#include <transactions/test/test_helper/CreateAccountTestHelper.h>
#include <transactions/test/test_helper/SetOptionsTestHelper.h>
#include <ledger/BalanceHelper.h>
#include <transactions/test/test_helper/ManageAssetTestHelper.h>
#include <transactions/test/test_helper/ManageBalanceTestHelper.h>
#include <lib/json/json.h>
#include <transactions/test/test_helper/ReviewKYCRequestTestHelper.h>
#include "overlay/LoopbackPeer.h"
#include "util/make_unique.h"
#include "main/test.h"
#include "ledger/AccountHelper.h"
#include "ledger/TrustFrame.h"
#include "ledger/TrustHelper.h"
#include "test/test_marshaler.h"

using namespace stellar;
using namespace stellar::txtest;

typedef std::unique_ptr<Application> appPtr;

// Try setting each option to make sure it works
// try setting all at once
// try setting high threshold ones without the correct sigs
// make sure it doesn't allow us to add signers when we don't have the
// minbalance
TEST_CASE("set options", "[tx][setoptions]")
{
    using xdr::operator==;

    Config const& cfg = getTestConfig(0, Config::TESTDB_POSTGRESQL);

    VirtualClock clock;
    Application::pointer appPtr = Application::create(clock, cfg);
    Application& app = *appPtr;
    app.start();

    // set up world
    Account root = Account{getRoot(), Salt(0)};
    auto testManager = TestManager::make(app);
    SetOptionsTestHelper setOptionsTestHelper(testManager);
    CreateAccountTestHelper createAccountTestHelper(testManager);
    ManageAssetTestHelper manageAssetTestHelper(testManager);
    ManageBalanceTestHelper manageBalanceTestHelper(testManager);
    ReviewKYCRequestTestHelper reviewKYCRequestTestHelper(testManager);

    //create account if user
    Account user = Account{SecretKey::random(), Salt(0)};
    createAccountTestHelper.applyCreateAccountTx(root, user.key.getPublicKey(), AccountType::GENERAL);

	auto accountHelper = AccountHelper::Instance();
	auto trustHelper = TrustHelper::Instance();

    SECTION("Signers")
    {
        ThresholdSetter th;
        th.masterWeight = make_optional<uint8_t>(100);
        th.lowThreshold = make_optional<uint8_t>(1);
        th.medThreshold = make_optional<uint8_t>(10);
        th.highThreshold = make_optional<uint8_t>(100);

        auto signerType = getAnySignerType()^static_cast<uint32_t>(SignerType::ACCOUNT_MANAGER);

        SECTION("Can't use non account manager signer for master weight OR threshold or signer")
		{
            //update thresholds
            setOptionsTestHelper.applySetOptionsTx(user, &th, nullptr, nullptr, nullptr);

            auto userAccount = AccountHelper::Instance()->mustLoadAccount(user.key.getPublicKey(), testManager->getDB());
            REQUIRE(userAccount->getHighThreshold() == *th.highThreshold);

            //add new signer
            SecretKey regularKP = SecretKey::random();
            Account regularAccount = Account{regularKP, Salt(0)};

            Signer regularSigner(regularKP.getPublicKey(), userAccount->getHighThreshold(), signerType, 2, "", Signer::_ext_t{});

            setOptionsTestHelper.applySetOptionsTx(user, nullptr, &regularSigner, nullptr, nullptr);

			SECTION("Can't add new signer")
			{
				SecretKey newSignerKP = SecretKey::random();
                Signer newSigner(newSignerKP.getPublicKey(), userAccount->getHighThreshold(), signerType, 2, "", Signer::_ext_t{});

                auto tx = setOptionsTestHelper.createSetOptionsTx(user, nullptr, &newSigner, nullptr, nullptr);

                //signed only by regularSigner:
                tx->getEnvelope().signatures.clear();
                tx->addSignature(regularKP);
                testManager->applyCheck(tx);

                auto opRes = tx->getResult().result.results()[0];
				REQUIRE(opRes.code() == OperationResultCode::opNOT_ALLOWED);
			}
			SECTION("Can't change threshold")
			{
                auto tx = setOptionsTestHelper.createSetOptionsTx(user, &th, nullptr, nullptr, nullptr);
                //signed only by regularSigner:
                tx->getEnvelope().signatures.clear();
                tx->addSignature(regularKP);
                testManager->applyCheck(tx);

                auto opRes = tx->getResult().result.results()[0];
				REQUIRE(opRes.code() == OperationResultCode::opNOT_ALLOWED);
			}

		}

        SECTION("can't use master key as alternate signer")
        {
            Signer self(user.key.getPublicKey(), 100, signerType, 0, "", Signer::_ext_t{});

            setOptionsTestHelper.applySetOptionsTx(user, nullptr, &self, nullptr, nullptr, SetOptionsResultCode::BAD_SIGNER);
        }

        SECTION("Can set and update signer name")
		{
			SecretKey regularKP = getAccount("regular");

			Signer regular(regularKP.getPublicKey(), 10, getAnySignerType(), 2, "", Signer::_ext_t{}); // high right regular account

			std::string name = "Test signer name";
			regular.name = name;
            setOptionsTestHelper.applySetOptionsTx(user, nullptr, &regular, nullptr, nullptr);

			//update signer name
			name += "New";
			regular.name = name;
            setOptionsTestHelper.applySetOptionsTx(user, nullptr, &regular, nullptr, nullptr);
		}

        SECTION("can not add Trust with same accountID")
        {
            TrustData trustData;
            TrustEntry trust;
            trust.allowedAccount = user.key.getPublicKey();
            trust.balanceToUse = user.key.getPublicKey();
            trustData.trust = trust;
            setOptionsTestHelper.applySetOptionsTx(user, nullptr, nullptr, &trustData, nullptr,
                                                   SetOptionsResultCode::TRUST_MALFORMED);
        }

        SECTION("can not add Trust if no balance")
        {
            auto newAccount = SecretKey::random();
            createAccountTestHelper.applyCreateAccountTx(root, newAccount.getPublicKey(), AccountType::GENERAL);

            TrustData trustData;
            TrustEntry trust;
            trust.allowedAccount = newAccount.getPublicKey();
            trust.balanceToUse = SecretKey::random().getPublicKey();
            trustData.trust = trust;

            setOptionsTestHelper.applySetOptionsTx(user, nullptr, nullptr, &trustData, nullptr,
                                                   SetOptionsResultCode::BALANCE_NOT_FOUND);
        }

        SECTION("can not add Trust if balance from wrong account")
        {
            auto newAccount = SecretKey::random();
            createAccountTestHelper.applyCreateAccountTx(root, newAccount.getPublicKey(), AccountType::GENERAL);

            TrustData trustData;
            TrustEntry trust;
            trust.allowedAccount = newAccount.getPublicKey();
            trust.balanceToUse = root.key.getPublicKey();
            trustData.trust = trust;

            setOptionsTestHelper.applySetOptionsTx(user, nullptr, nullptr, &trustData, nullptr,
                                                   SetOptionsResultCode::BALANCE_NOT_FOUND);
        }


        SECTION("can add Trust")
        {
            auto trustAccount = SecretKey::random();
            createAccountTestHelper.applyCreateAccountTx(root, trustAccount.getPublicKey(), AccountType::GENERAL);

            TrustData trustData;
            TrustEntry trust;
            trust.allowedAccount = trustAccount.getPublicKey();

            // create a balance for user
            AssetCode testAsset = "USD";
            manageAssetTestHelper.createAsset(root, root.key, testAsset, root, 0);
            auto userAccountID = user.key.getPublicKey();
            auto manageBalanceRes = manageBalanceTestHelper.applyManageBalanceTx(user, userAccountID, testAsset);
            auto balanceID = manageBalanceRes.success().balanceID;

            trust.balanceToUse = balanceID;

            trustData.trust = trust;
            trustData.action = ManageTrustAction::TRUST_ADD;

            setOptionsTestHelper.applySetOptionsTx(user, nullptr, nullptr, &trustData, nullptr);

            REQUIRE(trustHelper->exists(app.getDatabase(),
                trustAccount.getPublicKey(), balanceID));

            SECTION("can delete")
            {
                trustData.action = ManageTrustAction::TRUST_REMOVE;
                setOptionsTestHelper.applySetOptionsTx(user, nullptr, nullptr, &trustData, nullptr);

                REQUIRE(!trustHelper->exists(app.getDatabase(),
                    trustAccount.getPublicKey(), balanceID));
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

            SecretKey testSignerKP = SecretKey::random();
            Signer testSigner(testSignerKP.getPublicKey(), 1, signerType, 1, "", Signer::_ext_t{});


            // add signer
            setOptionsTestHelper.applySetOptionsTx(user, &th, &testSigner, nullptr, nullptr);
			checkSigner(app, testSigner, 1, user.key);

			// update weight
            testSigner.weight++;
            setOptionsTestHelper.applySetOptionsTx(user, &th, &testSigner, nullptr, nullptr);
			checkSigner(app, testSigner, 1, user.key);

			// update type
			testSigner.signerType = static_cast<int32_t>(SignerType::ACCOUNT_MANAGER);
            setOptionsTestHelper.applySetOptionsTx(user, &th, &testSigner, nullptr, nullptr);
			checkSigner(app, testSigner, 1, user.key);

			// update identity
			testSigner.identity++;
            setOptionsTestHelper.applySetOptionsTx(user, &th, &testSigner, nullptr, nullptr);
			checkSigner(app, testSigner, 1, user.key);

            // add new signer
            SecretKey testSigner2KP = SecretKey::random();
            Signer testSigner2 = Signer(testSigner2KP.getPublicKey(), 100, getAnySignerType(), 2, "", Signer::_ext_t{});
            setOptionsTestHelper.applySetOptionsTx(user, nullptr, &testSigner2, nullptr, nullptr);

			checkSigner(app, testSigner, 2, user.key);
			checkSigner(app, testSigner2, 2, user.key);

			// remove signer 1
            testSigner.weight = 0;
            setOptionsTestHelper.applySetOptionsTx(user, nullptr, &testSigner, nullptr, nullptr);
			checkSigner(app, testSigner2, 1, user.key);

            // remove signer 2
            testSigner2.weight = 0;
            setOptionsTestHelper.applySetOptionsTx(user, nullptr, &testSigner2, nullptr, nullptr);

            auto userAccount = accountHelper->mustLoadAccount(user.key.getPublicKey(), testManager->getDB());
            REQUIRE(userAccount->getAccount().signers.size() == 0);
        }
    }

    SECTION("update KYC")
    {
        UpdateKYCData updateKYC("{\n\"hash\": \"b11a4cff677bd778084ac04522730f0b7a72323c\"\n}", 0, UpdateKYCData::_ext_t{});
        SECTION("successfully create updateKYC request")
        {
            setOptionsTestHelper.applySetOptionsTx(user, nullptr, nullptr, nullptr, &updateKYC);
        }

        SECTION("malformed json")
        {
            //missed colon
            updateKYC.dataKYC = "{\n\"hash\" \"b11a4cff677bd778084ac04522730f0b7a72323c\"\n}";
            setOptionsTestHelper.applySetOptionsTx(user, nullptr, nullptr, nullptr, &updateKYC,
                                                   SetOptionsResultCode::UPDATE_KYC_MALFORMED);
        }

        SECTION("try to recreate request")
        {
            setOptionsTestHelper.applySetOptionsTx(user, nullptr, nullptr, nullptr, &updateKYC);

            //try to create new request:
            setOptionsTestHelper.applySetOptionsTx(user, nullptr, nullptr, nullptr, &updateKYC,
                                                   SetOptionsResultCode::UPDATE_KYC_REQUEST_NOT_FOUND);
        }

        SECTION("try to update non-existing request")
        {
            updateKYC.requestID = 1;
            setOptionsTestHelper.applySetOptionsTx(user, nullptr, nullptr, nullptr, &updateKYC,
                                                   SetOptionsResultCode::UPDATE_KYC_REQUEST_NOT_FOUND);
        }

        SECTION("successful review")
        {
            auto opResult = setOptionsTestHelper.applySetOptionsTx(user, nullptr, nullptr, nullptr, &updateKYC);
            uint64_t requestID = opResult.success().requestID;
            reviewKYCRequestTestHelper.applyReviewRequestTx(root, requestID, ReviewRequestOpAction::APPROVE, "");
        }

        SECTION("reject, update then approve")
        {
            auto opResult = setOptionsTestHelper.applySetOptionsTx(user, nullptr, nullptr, nullptr, &updateKYC);
            uint64_t requestID = opResult.success().requestID;

            reviewKYCRequestTestHelper.applyReviewRequestTx(root, requestID, ReviewRequestOpAction::REJECT, "invalid KYC data");

            updateKYC.requestID = requestID;
            updateKYC.dataKYC = "{\n\"hash\" : \"74736131aad9bf49ad7224c70f6a7cda5832641b\"\n}";
            setOptionsTestHelper.applySetOptionsTx(user, nullptr, nullptr, nullptr, &updateKYC);

            reviewKYCRequestTestHelper.applyReviewRequestTx(root, requestID, ReviewRequestOpAction::APPROVE, "");
        }

        SECTION("try to reject permanently")
        {
            auto opRes = setOptionsTestHelper.applySetOptionsTx(user, nullptr, nullptr, nullptr, &updateKYC);
            uint64_t requestID = opRes.success().requestID;

            reviewKYCRequestTestHelper.applyReviewRequestTx(root, requestID, ReviewRequestOpAction::PERMANENT_REJECT, "invalid KYC data",
                                                            ReviewRequestResultCode::PERMANENT_REJECT_NOT_ALLOWED);
        }
    }

    // these are all tested by other tests
    // set transfer rate
    // set data
    // set thresholds
    // set signer
}
