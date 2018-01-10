// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "main/Application.h"
#include "util/Timer.h"
#include "overlay/LoopbackPeer.h"
#include "util/make_unique.h"
#include "main/test.h"
#include "lib/json/json.h"
#include "ledger/LedgerManager.h"
#include "ledger/LedgerDelta.h"
#include "transactions/PaymentOpFrame.h"
#include "transactions/SetOptionsOpFrame.h"
#include "transactions/CreateAccountOpFrame.h"
#include "transactions/manage_asset/ManageAssetOpFrame.h"
#include "transactions/ManageBalanceOpFrame.h"
#include "transactions/test/TxTests.h"
#include "crypto/SHA.h"
#include "test/test_marshaler.h"

using namespace stellar;
using namespace stellar::txtest;

typedef std::unique_ptr<Application> appPtr;

/*
  Tests that are testing the common envelope used in transactions.
  Things like:
    authz/authn
    double spend
*/

TEST_CASE("txenvelope", "[dep_tx][envelope]")
{
    auto cfg = getTestConfig(0, Config::TESTDB_POSTGRESQL);

    VirtualClock clock;
    Application::pointer appPtr = Application::create(clock, cfg);
    Application& app = *appPtr;
    Hash const& networkID = app.getNetworkID();
    app.start();
    closeLedgerOn(app, 2, 1, 7, 2014);

    // set up world
    SecretKey root = getRoot();
    SecretKey issuance = getIssuanceKey();
    SecretKey a1 = getAccount("A");
    Salt rootSeq = 1;

    AssetCode asset = app.getBaseAsset();
    SECTION("outer envelope")
    {
        TransactionFramePtr txFrame;
        LedgerDelta delta(app.getLedgerManager().getCurrentLedgerHeader(),
                          app.getDatabase());

        SECTION("no signature")
        {
            txFrame = createCreateAccountTx(networkID, root, a1, rootSeq++, AccountType::GENERAL);
            txFrame->getEnvelope().signatures.clear();

            applyCheck(txFrame, delta, app);

            REQUIRE(txFrame->getResultCode() == TransactionResultCode::txBAD_AUTH);
        }
        SECTION("bad signature")
        {
            Signature fakeSig = root.sign("fake data");
            txFrame = createCreateAccountTx(networkID, root, a1, rootSeq++, AccountType::GENERAL);
            txFrame->getEnvelope().signatures[0].signature = fakeSig;

            applyCheck(txFrame, delta, app);

            REQUIRE(txFrame->getResultCode() == TransactionResultCode::txBAD_AUTH_EXTRA);
        }
        SECTION("bad signature (wrong hint)")
        {
            txFrame = createCreateAccountTx(networkID, root, a1, rootSeq++, AccountType::GENERAL);
            txFrame->getEnvelope().signatures[0].hint.fill(1);

            applyCheck(txFrame, delta, app);

            REQUIRE(txFrame->getResultCode() == TransactionResultCode::txBAD_AUTH_EXTRA);
        }
        SECTION("too many signatures (signed twice)")
        {
            txFrame = createCreateAccountTx(networkID, root, a1, rootSeq++, AccountType::GENERAL);
            txFrame->addSignature(a1);

            applyCheck(txFrame, delta, app);

            REQUIRE(txFrame->getResultCode() == TransactionResultCode::txBAD_AUTH_EXTRA);
        }
        SECTION("too many signatures (unused signature)")
        {
            txFrame = createCreateAccountTx(networkID, root, a1, rootSeq++, AccountType::GENERAL);
            SecretKey bogus = getAccount("bogus");
            txFrame->addSignature(bogus);

            applyCheck(txFrame, delta, app);

            REQUIRE(txFrame->getResultCode() == TransactionResultCode::txBAD_AUTH_EXTRA);
        }
    }

    SECTION("multisig")
    {
        applyCreateAccountTx(app, root, a1, rootSeq++, AccountType::GENERAL);
        Salt a1Seq = 1;

        SecretKey s1 = getAccount("S1");
        Signer sk1(s1.getPublicKey(), 5, getAnySignerType(), 1, "", Signer::_ext_t{}); // below low rights

        ThresholdSetter th;

        th.masterWeight = make_optional<uint8_t>(100);
        th.lowThreshold = make_optional<uint8_t>(10);
        th.medThreshold = make_optional<uint8_t>(50);
        th.highThreshold = make_optional<uint8_t>(100);

        applySetOptions(app, a1, a1Seq++, &th, &sk1);

        SecretKey s2 = getAccount("S2");
        Signer sk2(s2.getPublicKey(), 95, getAnySignerType(), 2,  "", Signer::_ext_t{}); // med rights account

        applySetOptions(app, a1, a1Seq++, nullptr, &sk2);

		SECTION("Commission can be signed by master")
		{
			auto account = SecretKey::random();
			applyCreateAccountTx(app, root, account, rootSeq++, AccountType::GENERAL);
			int64_t amount = app.getConfig().EMISSION_UNIT;
			Salt s = 0;
			fundAccount(app, root, issuance, s, account.getPublicKey(), amount);
			auto commission = getCommissionKP();
			applyPaymentTx(app, account, commission, 0, amount, getNoPaymentFee(), false);
			auto commissionSeq = 1;
			auto tx = createPaymentTx(app.getNetworkID(), commission, account, commissionSeq++, amount, getNoPaymentFee());
			LedgerDelta delta(app.getLedgerManager().getCurrentLedgerHeader(),
				app.getDatabase());
			SECTION("Master can sign")
			{
				tx->getEnvelope().signatures.clear();
				tx->addSignature(root);
				REQUIRE(applyCheck(tx, delta, app));
			}
			SECTION("Commission can't")
			{
				REQUIRE(!applyCheck(tx, delta, app));
			}
		}
        SECTION("not enough rights (envelope)")
        {
            TransactionFramePtr tx =
                createPaymentTx(networkID, a1, root, a1Seq++, 1000, getNoPaymentFee());

            // only sign with s1
            tx->getEnvelope().signatures.clear();
            tx->addSignature(s1);

            LedgerDelta delta(app.getLedgerManager().getCurrentLedgerHeader(),
                              app.getDatabase());

            applyCheck(tx, delta, app);
            REQUIRE(tx->getResultCode() == TransactionResultCode::txBAD_AUTH);
        }

        SECTION("not enough rights (operation)")
        {
            // updating thresholds requires high
            TransactionFramePtr tx =
                createSetOptions(networkID, a1, a1Seq++, &th, &sk1);

            // only sign with s1 (med)
            tx->getEnvelope().signatures.clear();
            tx->addSignature(s2);

            LedgerDelta delta(app.getLedgerManager().getCurrentLedgerHeader(),
                              app.getDatabase());

            applyCheck(tx, delta, app);
            REQUIRE(tx->getResultCode() == TransactionResultCode::txFAILED);
            REQUIRE(getFirstResultCode(*tx) == OperationResultCode::opBAD_AUTH);
        }

		SECTION("Same identity can't sign twice")
		{
			SecretKey s1Bakup = getAccount("S1Backup");
			Signer sk1Backup(s1Bakup.getPublicKey(), 95, getAnySignerType(), 1, "", Signer::_ext_t{}); // med rights
			applySetOptions(app, a1, a1Seq++, nullptr, &sk1Backup);


            TransactionFramePtr tx =
                createPaymentTx(networkID, a1, root, a1Seq++, 1000, getNoPaymentFee());

			tx->getEnvelope().signatures.clear();
			tx->addSignature(s1);
			tx->addSignature(s1Bakup);

			LedgerDelta delta(app.getLedgerManager().getCurrentLedgerHeader(),
				app.getDatabase());

			applyCheck(tx, delta, app);
			REQUIRE(tx->getResultCode() == TransactionResultCode::txBAD_AUTH_EXTRA);
		}

        SECTION("success two signatures")
        {
			// fund account account
			int64_t paymentAmount = app.getConfig().EMISSION_UNIT;
			auto emissionAmount = paymentAmount;
    
            fundAccount(app, root, issuance, rootSeq, a1.getPublicKey(), emissionAmount);

			auto commission = getCommissionKP();
            TransactionFramePtr tx =
                createPaymentTx(networkID, a1, commission, a1Seq++, paymentAmount,
                    getNoPaymentFee());

            tx->getEnvelope().signatures.clear();
            tx->addSignature(s1);
            tx->addSignature(s2);

            LedgerDelta delta(app.getLedgerManager().getCurrentLedgerHeader(),
                              app.getDatabase());

            applyCheck(tx, delta, app);
            REQUIRE(tx->getResultCode() == TransactionResultCode::txSUCCESS);
            REQUIRE(PaymentOpFrame::getInnerCode(getFirstResult(*tx)) ==
                    PaymentResultCode::SUCCESS);
        }
    }

    SECTION("batching")
    {
        SECTION("empty batch")
        {
            TransactionEnvelope te;
            te.tx.sourceAccount = root.getPublicKey();
            te.tx.salt = rootSeq++;
            TransactionFramePtr tx =
                std::make_shared<TransactionFrame>(networkID, te);
            tx->addSignature(root);
            LedgerDelta delta(app.getLedgerManager().getCurrentLedgerHeader(),
                              app.getDatabase());

            REQUIRE(!tx->checkValid(app));

            applyCheck(tx, delta, app);
            REQUIRE(tx->getResultCode() == TransactionResultCode::txMISSING_OPERATION);
        }

        SECTION("non empty")
        {
            SecretKey b1 = getAccount("B");
            applyCreateAccountTx(app, root, a1, rootSeq++, AccountType::GENERAL);
            applyCreateAccountTx(app, root, b1, rootSeq++, AccountType::GENERAL);

            Salt a1Seq = 1;
            Salt b1Seq = 1;

            SECTION("single tx wrapped by different account")
            {
				auto details = "some json data";
				auto requestID = binToHex(sha256(details));
                auto balanceID = SecretKey::random().getPublicKey();
                auto amount = app.getConfig().EMISSION_UNIT;
                fundAccount(app, root, issuance, rootSeq, a1.getPublicKey(), amount);
                TransactionFramePtr tx = createPaymentTx(networkID, a1, b1, a1Seq++,
                    amount / 2, getNoPaymentFee() ,false, "subj", "ref");

                // change inner payment to be b->root
                tx->getEnvelope().tx.operations[0].sourceAccount.activate() =
                    b1.getPublicKey();

                tx->getEnvelope().signatures.clear();
                tx->addSignature(a1);

                SECTION("missing signature")
                {
                    LedgerDelta delta(
                        app.getLedgerManager().getCurrentLedgerHeader(),
                        app.getDatabase());

                    REQUIRE(!tx->checkValid(app));
                    applyCheck(tx, delta, app);
                    REQUIRE(tx->getResultCode() == TransactionResultCode::txFAILED);
                    REQUIRE(tx->getOperations()[0]->getResultCode() ==
                            OperationResultCode::opBAD_AUTH);
                }

                SECTION("success signature check")
                {
                    tx->addSignature(b1);
                    LedgerDelta delta(
                        app.getLedgerManager().getCurrentLedgerHeader(),
                        app.getDatabase());

                    REQUIRE(tx->checkValid(app));
                    applyCheck(tx, delta, app);
                    REQUIRE(tx->getResultCode() == TransactionResultCode::txFAILED);
                    REQUIRE(PaymentOpFrame::getInnerCode(getFirstResult(*tx)) ==
                            PaymentResultCode::BALANCE_ACCOUNT_MISMATCHED);
                }
            }
            SECTION("multiple tx")
            {
            
                ThresholdSetter th;

                th.masterWeight = make_optional<uint8_t>(100);

                TransactionFramePtr tx_a =
                    createSetOptions(networkID, a1, a1Seq++,&th, nullptr);
                SECTION("one invalid tx")
                {
					Signer sk(b1.getPublicKey(), 100, getAnySignerType(), 0, "", Signer::_ext_t{});

					// invalid amount
                    TransactionFramePtr tx_b =
                        createSetOptions(networkID, b1, b1Seq++, nullptr, &sk);
                    // build a new tx based off tx_a and tx_b
                    tx_b->getEnvelope()
                        .tx.operations[0]
                        .sourceAccount.activate() = b1.getPublicKey();
                    tx_a->getEnvelope().tx.operations.push_back(
                        tx_b->getEnvelope().tx.operations[0]);
                    TransactionFramePtr tx =
                        TransactionFrame::makeTransactionFromWire(
                            networkID, tx_a->getEnvelope());

                    tx->getEnvelope().signatures.clear();
                    tx->addSignature(a1);
                    tx->addSignature(b1);

                    LedgerDelta delta(
                        app.getLedgerManager().getCurrentLedgerHeader(),
                        app.getDatabase());

                    REQUIRE(!tx->checkValid(app));

                    applyCheck(tx, delta, app);

                    REQUIRE(tx->getResultCode() == TransactionResultCode::txFAILED);
                    // first operation was success
                    REQUIRE(SetOptionsOpFrame::getInnerCode(getFirstResult(*tx)) ==
                            SetOptionsResultCode::SUCCESS);
                    // second
                    REQUIRE(SetOptionsOpFrame::getInnerCode(
                                tx->getOperations()[1]->getResult()) ==
						SetOptionsResultCode::BAD_SIGNER);
                }
                SECTION("one failed tx")
                {
					// not found to delete
					TransactionFramePtr tx_b = createManageBalanceTx(networkID, b1, b1, b1Seq++, "AETH", ManageBalanceAction::DELETE_BALANCE);

                    tx_b->getEnvelope()
                        .tx.operations[0]
                        .sourceAccount.activate() = b1.getPublicKey();
                    tx_a->getEnvelope().tx.operations.push_back(
                        tx_b->getEnvelope().tx.operations[0]);

                    TransactionFramePtr tx =
                        TransactionFrame::makeTransactionFromWire(
                            networkID, tx_a->getEnvelope());

                    tx->getEnvelope().signatures.clear();
                    tx->addSignature(a1);
                    tx->addSignature(b1);

                    LedgerDelta delta(
                        app.getLedgerManager().getCurrentLedgerHeader(),
                        app.getDatabase());

                    REQUIRE(tx->checkValid(app));

                    applyCheck(tx, delta, app);

                    REQUIRE(tx->getResultCode() == TransactionResultCode::txFAILED);
                    // first operation was success
					REQUIRE(SetOptionsOpFrame::getInnerCode(getFirstResult(*tx)) ==
						SetOptionsResultCode::SUCCESS);
                    REQUIRE(ManageBalanceOpFrame::getInnerCode(
                                    tx->getOperations()[1]->getResult()) ==
                        ManageBalanceResultCode::MALFORMED);
                }
                SECTION("both success")
                {
                    auto b1signer = SecretKey::random();
                    Signer sk(b1signer.getPublicKey(), 100, getAnySignerType(), 0, "", Signer::_ext_t{});

					// invalid amount
                    TransactionFramePtr tx_b =
                        createSetOptions(networkID, b1, b1Seq++, nullptr, &sk);

                    tx_b->getEnvelope()
                        .tx.operations[0]
                        .sourceAccount.activate() = b1.getPublicKey();
                    tx_a->getEnvelope().tx.operations.push_back(
                        tx_b->getEnvelope().tx.operations[0]);
                    TransactionFramePtr tx =
                        TransactionFrame::makeTransactionFromWire(
                            app.getNetworkID(), tx_a->getEnvelope());

                    tx->getEnvelope().signatures.clear();
                    tx->addSignature(a1);
                    tx->addSignature(b1);

                    LedgerDelta delta(
                        app.getLedgerManager().getCurrentLedgerHeader(),
                        app.getDatabase());

                    REQUIRE(tx->checkValid(app));

                    applyCheck(tx, delta, app);

                    REQUIRE(tx->getResultCode() == TransactionResultCode::txSUCCESS);

					REQUIRE(SetOptionsOpFrame::getInnerCode(getFirstResult(*tx)) ==
						SetOptionsResultCode::SUCCESS);
                    REQUIRE(SetOptionsOpFrame::getInnerCode(
                                tx->getOperations()[1]->getResult()) ==
						SetOptionsResultCode::SUCCESS);
                }
            }
        }
    }

    SECTION("common transaction")
    {
		auto closeLedger = [&app](time_t closeTime, TransactionFramePtr tx) {
			auto ledgerSeq = app.getLedgerManager().getLedgerNum();
			TxSetFramePtr txSet = std::make_shared<TxSetFrame>(
				app.getLedgerManager().getLastClosedLedgerHeader().hash);
			if (tx)
			{
				txSet->add(tx);
			}
			closeLedgerOn(app, ledgerSeq, closeTime, txSet);
		};
		auto applyCheckTxFrame = [&app, &root, &a1, &closeLedger](TransactionFramePtr txFrame, TransactionResultCode expectedResult) {
			LedgerDelta delta(app.getLedgerManager().getCurrentLedgerHeader(),
				app.getDatabase());

			applyCheck(txFrame, delta, app);			
			REQUIRE(txFrame->getResultCode() == expectedResult);
			if (expectedResult == TransactionResultCode::txSUCCESS)
			{
				VirtualClock::time_point tp = VirtualClock::tmToPoint(app.getLedgerManager().getTmCloseTime());
				time_t t = VirtualClock::to_time_t(tp);
				closeLedger(t, txFrame);
			}

			return txFrame;
		};

		VirtualClock::time_point ledgerTime;
		time_t start = getTestDate(1, 7, 2014);
		ledgerTime = VirtualClock::from_time_t(start);
		clock.setCurrentTime(ledgerTime);

        {

            SECTION("duplicate payment")
            {
				auto validUntill = start + 5;
				auto txFrame = createCreateAccountTx(app.getNetworkID(), root, a1, 0, AccountType::GENERAL);
				txFrame->getEnvelope().tx.timeBounds = TimeBounds(0, validUntill);
				txFrame->getEnvelope().signatures.clear();
				txFrame->addSignature(root);

				
				applyCheckTxFrame(txFrame, TransactionResultCode::txSUCCESS);
				// try submit same transaction
				applyCheckTxFrame(txFrame, TransactionResultCode::txDUPLICATION);

				applyCheckTxFrame(txFrame, TransactionResultCode::txDUPLICATION);
				// some time has passed
				validUntill++;
				closeLedger(validUntill, nullptr);

				applyCheckTxFrame(txFrame, TransactionResultCode::txTOO_LATE);
            }

           SECTION("time issues")
            {
                // tx too young
                // tx ok
                // tx too old
			   auto txFrame = createCreateAccountTx(app.getNetworkID(), root, a1, 0, AccountType::GENERAL);
			   auto setTimeBounds = [](TransactionFramePtr txFrame, SecretKey signer, TimeBounds tb) {
				   txFrame->getEnvelope().tx.timeBounds = tb;
				   txFrame->getEnvelope().signatures.clear();
				   txFrame->addSignature(signer);
			   };

			   setTimeBounds(txFrame, root, TimeBounds(start + 1, start + 10));
			   applyCheckTxFrame(txFrame, TransactionResultCode::txTOO_EARLY);

			   setTimeBounds(txFrame, root, TimeBounds(start, start + 10));
			   applyCheckTxFrame(txFrame, TransactionResultCode::txSUCCESS);

			   setTimeBounds(txFrame, root, TimeBounds(start - 10, start - 1));
			   applyCheckTxFrame(txFrame, TransactionResultCode::txTOO_LATE);
            }

        }
    }
}
