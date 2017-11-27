// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "main/Application.h"
#include "main/Config.h"
#include "util/Timer.h"
#include "overlay/LoopbackPeer.h"
#include "util/make_unique.h"
#include "main/test.h"
#include "lib/catch.hpp"
#include "util/Logging.h"
#include "TxTests.h"
#include "transactions/TransactionFrame.h"
#include "ledger/LedgerDelta.h"
#include "ledger/TrustFrame.h"

using namespace stellar;
using namespace stellar::txtest;

typedef std::unique_ptr<Application> appPtr;

// Try setting each option to make sure it works
// try setting all at once
// try setting high threshold ones without the correct sigs
// make sure it doesn't allow us to add signers when we don't have the
// minbalance
TEST_CASE("set options", "[dep_tx][setoptions]")
{
    using xdr::operator==;

    Config const& cfg = getTestConfig(0, Config::TESTDB_POSTGRESQL);

    VirtualClock clock;
    Application::pointer appPtr = Application::create(clock, cfg);
    Application& app = *appPtr;
    app.start();
    closeLedgerOn(app, 2, 1, 7, 2014);

	upgradeToCurrentLedgerVersion(app);

    // set up world
    SecretKey root = getRoot();
    SecretKey a1 = getAccount("A");

    Salt rootSeq = 1;

    applyCreateAccountTx(app, root, a1, rootSeq++, GENERAL);

    Salt a1seq = 1;

    SECTION("Signers")
    {
        SecretKey s1 = getAccount("S1");
        Signer sk1(s1.getPublicKey(), 1, getAnySignerType() & ~SIGNER_ACCOUNT_MANAGER, 1, "", Signer::_ext_t{}); // low right account

        ThresholdSetter th;

        th.masterWeight = make_optional<uint8_t>(100);
        th.lowThreshold = make_optional<uint8_t>(1);
        th.medThreshold = make_optional<uint8_t>(10);
        th.highThreshold = make_optional<uint8_t>(100);

		SECTION("Can't use non account manager signer for master weight OR threshold or signer")
		{
			applySetOptions(app, a1, a1seq++, &th, nullptr);

			SecretKey regularKP = getAccount("regular");
			auto a1Account = loadAccount(a1, app);
			Signer regular(regularKP.getPublicKey(), a1Account->getHighThreshold(), getAnySignerType() & ~SIGNER_ACCOUNT_MANAGER, 2, "", Signer::_ext_t{}); // high right regular account
			applySetOptions(app, a1, a1seq++, nullptr, &regular);

			LedgerDelta delta(app.getLedgerManager().getCurrentLedgerHeader(),
				app.getDatabase());

			SECTION("Can't add new signer")
			{
				SecretKey s2KP = getAccount("s2");
				Signer s2(s2KP.getPublicKey(), a1Account->getHighThreshold(), getAnySignerType() & ~SIGNER_ACCOUNT_MANAGER, 2, "", Signer::_ext_t{}); // high right regular account
				auto tx = createSetOptions(app.getNetworkID(), a1, a1seq++, nullptr, &s2);
				tx->getEnvelope().signatures.clear();
				tx->addSignature(regularKP);
				applyCheck(tx, delta, app);
				REQUIRE(getFirstResult(*tx).code() == opNOT_ALLOWED);
			}
			SECTION("Can't change threshold")
			{
				auto tx = createSetOptions(app.getNetworkID(), a1, a1seq++, &th, nullptr);
				tx->getEnvelope().signatures.clear();
				tx->addSignature(regularKP);
				applyCheck(tx, delta, app);
				REQUIRE(getFirstResult(*tx).code() == opNOT_ALLOWED);
			}

		}
        SECTION("can't use master key as alternate signer")
        {
            Signer sk(a1.getPublicKey(), 100, getAnySignerType() & ~SIGNER_ACCOUNT_MANAGER, 0, "", Signer::_ext_t{});
            applySetOptions(app, a1, a1seq++, nullptr, &sk, nullptr, SET_OPTIONS_BAD_SIGNER);
        }

		LedgerDelta delta(app.getLedgerManager().getCurrentLedgerHeader(),
			app.getDatabase());
		auto checkSignerName = [&app, &delta](AccountID accountID, Signer expectedSigner) {
			auto account = AccountFrame::loadAccount(accountID, app.getDatabase(), &delta);
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
			auto a1Account = loadAccount(a1, app);
			Signer regular(regularKP.getPublicKey(), 10, getAnySignerType(), 2, "", Signer::_ext_t{}); // high right regular account

			std::string name = "Test signer name";
			regular.name = name;
			applySetOptions(app, a1, a1seq++, nullptr, &regular);
			checkSignerName(a1.getPublicKey(), regular);

			//update signer name
			name += "New";
			regular.name = name;
			applySetOptions(app, a1, a1seq++, nullptr, &regular);
			checkSignerName(a1.getPublicKey(), regular);

		}
        SECTION("can not add Trust with same accountID")
        {
            TrustData trustData;
            TrustEntry trust;
            trust.allowedAccount = a1.getPublicKey();
            trust.balanceToUse = a1.getPublicKey();
            trustData.trust = trust;
            applySetOptions(app, a1, a1seq++, nullptr, nullptr, &trustData,
                SET_OPTIONS_TRUST_MALFORMED);
        }

        SECTION("can not add Trust if no balance")
        {
            auto newAccount = SecretKey::random();
            applyCreateAccountTx(app, root, newAccount, rootSeq++, GENERAL);

            TrustData trustData;
            TrustEntry trust;
            trust.allowedAccount = newAccount.getPublicKey();
            trust.balanceToUse = SecretKey::random().getPublicKey();
            trustData.trust = trust;

            applySetOptions(app, a1, a1seq++, nullptr, nullptr, &trustData,
                SET_OPTIONS_BALANCE_NOT_FOUND);
        }
        SECTION("can not add Trust if balance from wrong account")
        {
            auto newAccount = SecretKey::random();
            applyCreateAccountTx(app, root, newAccount, rootSeq++, GENERAL);

            TrustData trustData;
            TrustEntry trust;
            trust.allowedAccount = newAccount.getPublicKey();
            trust.balanceToUse = root.getPublicKey();
            trustData.trust = trust;

            applySetOptions(app, a1, a1seq++, nullptr, nullptr, &trustData,
                SET_OPTIONS_BALANCE_NOT_FOUND);
        }


        SECTION("can add Trust")
        {
            auto trustAccount = SecretKey::random();
            REQUIRE(!TrustFrame::exists(app.getDatabase(),
                trustAccount.getPublicKey(), a1.getPublicKey()));

            applyCreateAccountTx(app, root, trustAccount, rootSeq++, GENERAL);

            TrustData trustData;
            TrustEntry trust;
            trust.allowedAccount = trustAccount.getPublicKey();
            trust.balanceToUse = a1.getPublicKey();
            trustData.trust = trust;
            trustData.action = TRUST_ADD;
            applySetOptions(app, a1, a1seq++, nullptr, nullptr, &trustData);
            
            REQUIRE(TrustFrame::exists(app.getDatabase(),
                trustAccount.getPublicKey(), a1.getPublicKey()));

            SECTION("can delete")
            {
                trustData.action = TRUST_REMOVE;
                applySetOptions(app, a1, a1seq++, nullptr, nullptr, &trustData);

                REQUIRE(!TrustFrame::exists(app.getDatabase(),
                    trustAccount.getPublicKey(), a1.getPublicKey()));
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

            applySetOptions(app, a1, a1seq++, &th, &sk1);
			// add signer
			checkSigner(app, sk1, 1, a1);

			// update weight
			sk1.weight = sk1.weight + 1;
			applySetOptions(app, a1, a1seq++, &th, &sk1);
			checkSigner(app, sk1, 1, a1);

			// update type
			sk1.signerType = SignerType::SIGNER_ACCOUNT_MANAGER;
			applySetOptions(app, a1, a1seq++, &th, &sk1);
			checkSigner(app, sk1, 1, a1);

			// update identity
			sk1.identity = sk1.identity + 1;
			applySetOptions(app, a1, a1seq++, &th,
				&sk1);
			checkSigner(app, sk1, 1, a1);


            // add signer 2
            SecretKey s2 = getAccount("S2");
            Signer sk2(s2.getPublicKey(), 100, getAnySignerType(), 2, "", Signer::_ext_t{});
            applySetOptions(app, a1, a1seq++, nullptr, &sk2);

			checkSigner(app, sk1, 2, a1);
			checkSigner(app, sk2, 2, a1);

			// remove signer 1
            sk1.weight = 0;
            applySetOptions(app, a1, a1seq++, nullptr, &sk1);
			checkSigner(app, sk2, 1, a1);

            // remove signer 2
            sk2.weight = 0;
            applySetOptions(app, a1, a1seq++, nullptr, &sk2);

            auto a1Account = loadAccount(a1, app);
            REQUIRE(a1Account->getAccount().signers.size() == 0);
        }
    }

    // these are all tested by other tests
    // set transfer rate
    // set data
    // set thresholds
    // set signer
}
