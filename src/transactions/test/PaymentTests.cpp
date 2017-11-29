// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0
#include "main/Application.h"
#include "util/Timer.h"
#include "main/Config.h"
#include "overlay/LoopbackPeer.h"
#include "main/test.h"
#include "lib/catch.hpp"
#include "TxTests.h"
#include "database/Database.h"
#include "ledger/LedgerManager.h"
#include "ledger/LedgerDelta.h"
#include "ledger/ReferenceFrame.h"
#include "transactions/PaymentOpFrame.h"
#include "crypto/SHA.h"

using namespace stellar;
using namespace stellar::txtest;

typedef std::unique_ptr<Application> appPtr;

TEST_CASE("payment", "[dep_tx][payment]")
{
	Config const& cfg = getTestConfig(0, Config::TESTDB_POSTGRESQL);

    VirtualClock clock;
    Application::pointer appPtr = Application::create(clock, cfg);
    Application& app = *appPtr;
    app.start();
    closeLedgerOn(app, 2, 1, 7, 2014);
	upgradeToCurrentLedgerVersion(app);
    // set up world
    SecretKey root = getRoot();
    SecretKey issuance = getIssuanceKey();

	int64 paymentAmount = 10 * app.getConfig().EMISSION_UNIT;

    Salt rootSeq = 1;
    LedgerDelta delta(app.getLedgerManager().getCurrentLedgerHeader(),
                      app.getDatabase());

	auto emissionAmount = 2 * paymentAmount;
    
	// fund some account

    // aWM stends for account with money
    auto aWM = SecretKey::random();
    applyCreateAccountTx(app, root, aWM, rootSeq++, AccountType::GENERAL);
    fundAccount(app, root, issuance, rootSeq, aWM.getPublicKey(), emissionAmount);

    auto secondAsset = "AETH";

	SECTION("basic tests")
	{
		auto account = SecretKey::random();
		applyCreateAccountTx(app, root, account, rootSeq++, AccountType::GENERAL);
		REQUIRE(getBalance(account.getPublicKey(), app) == 0);
		REQUIRE(getBalance(aWM.getPublicKey(), app) ==
            emissionAmount);

		auto paymentResult = applyPaymentTx(app, aWM, account,
            rootSeq++, paymentAmount, getNoPaymentFee(), false);
		REQUIRE(getBalance(account.getPublicKey(), app) == paymentAmount);
		REQUIRE(getBalance(aWM.getPublicKey(), app) == (emissionAmount - paymentAmount));
        REQUIRE(paymentResult.paymentResponse().destination == account.getPublicKey());
        REQUIRE(paymentResult.paymentResponse().asset == app.getBaseAsset());

		// send back
		auto accountSeq = 1;
		paymentResult = applyPaymentTx(app, account, aWM, accountSeq++, paymentAmount, getNoPaymentFee(), false);
		REQUIRE(getBalance(account.getPublicKey(), app) == 0);
		REQUIRE(getBalance(aWM.getPublicKey(), app) == emissionAmount);
        REQUIRE(paymentResult.paymentResponse().destination == aWM.getPublicKey());
        REQUIRE(paymentResult.paymentResponse().asset == app.getBaseAsset());

        auto paymentID = paymentResult.paymentResponse().paymentID;
        soci::session& sess = app.getDatabase().getSession(); 
        REQUIRE(PaymentRequestFrame::countObjects(sess) == 0);
	}
    SECTION("send to self")
    {
        auto balanceBefore = getAccountBalance(aWM, app);
        applyPaymentTx(app, aWM, aWM, rootSeq++, paymentAmount, getNoPaymentFee(), false, "", "", PaymentResultCode::MALFORMED);
    }
	SECTION("Malformed")
	{
		auto account = SecretKey::random();
		applyCreateAccountTx(app, root, account, rootSeq++, AccountType::GENERAL);
		SECTION("Negative amount")
		{
			applyPaymentTx(app, aWM, account, rootSeq++, -100, getNoPaymentFee(), false, "", "", PaymentResultCode::MALFORMED);
		}
		SECTION("Zero amount")
		{
			applyPaymentTx(app, aWM, account, rootSeq++, 0, getNoPaymentFee(),
                false, "", "",PaymentResultCode::MALFORMED);
		}
	}
	SECTION("PAYMENT_UNDERFUNDED")
	{
		auto account = SecretKey::random();
		applyCreateAccountTx(app, root, account, rootSeq++, AccountType::GENERAL);
		applyPaymentTx(app, aWM, account, rootSeq++, emissionAmount+1, getNoPaymentFee(),
            false, "", "", PaymentResultCode::UNDERFUNDED);
	}
	SECTION("Destination does not exist")
	{
		auto account = SecretKey::random();
		applyPaymentTx(app, aWM, account, rootSeq++, emissionAmount, getNoPaymentFee(), 
            false, "","", PaymentResultCode::BALANCE_NOT_FOUND);
	}
    SECTION("Payment between different assets are not supported")
    {
		auto paymentAmount = 600;
        auto account = SecretKey::random();
		auto accSeq = 1;
		auto balanceID = SecretKey::random().getPublicKey();
		std::string accountID = PubKeyUtils::toStrKey(account.getPublicKey());
        
        applyCreateAccountTx(app, root, account, rootSeq++, AccountType::GENERAL);

        applyManageAssetTx(app, root, rootSeq++, secondAsset);

		applyManageBalanceTx(app, account, account, accSeq++, balanceID, secondAsset);

        auto accBalanceForSecondAsset = BalanceFrame::loadBalance(account.getPublicKey(), secondAsset, app.getDatabase(), &delta);

        auto paymentResult = applyPaymentTx(app, aWM, aWM.getPublicKey(), accBalanceForSecondAsset->getBalanceID(), rootSeq++,
            paymentAmount, getNoPaymentFee(), false, "", "", PaymentResultCode::BALANCE_ASSETS_MISMATCHED);
    }
	SECTION("Payment fee")
    {
		int64 feeAmount = 2 * ONE; // fee is 2%
		int64_t fixedFee = 3;
        
        auto feeFrame = FeeFrame::create(FeeType::PAYMENT_FEE, fixedFee, feeAmount, app.getBaseAsset());
        auto fee = feeFrame->getFee();
        
		applySetFees(app, root, rootSeq++, &fee, false, nullptr);
		auto account = SecretKey::random();
		applyCreateAccountTx(app, root, account, rootSeq++, AccountType::GENERAL);
		auto accountSeq = 1;
		int64 balance = 60 * app.getConfig().EMISSION_UNIT;
		paymentAmount = 6 * app.getConfig().EMISSION_UNIT;
        PaymentFeeData paymentFee = getGeneralPaymentFee(fixedFee, paymentAmount * (feeAmount / ONE) / 100);
		fundAccount(app, root, issuance, rootSeq, account.getPublicKey(), balance);
		auto dest = SecretKey::random();
		applyCreateAccountTx(app, root, dest, rootSeq++, AccountType::GENERAL);
		SECTION("Fee mismatched")
		{
			auto invalidFee = paymentFee;
			invalidFee.sourceFee.paymentFee -= 1;
			applyPaymentTx(app, account, dest, accountSeq++, paymentAmount, invalidFee,
                false, "", "", PaymentResultCode::FEE_MISMATCHED);
			invalidFee = paymentFee;
			invalidFee.sourceFee.fixedFee -= 1;
			applyPaymentTx(app, account, dest, accountSeq++, paymentAmount, invalidFee,
				false, "", "", PaymentResultCode::FEE_MISMATCHED);
		}
        BalanceID commissionBalanceID = getCommissionKP().getPublicKey();
        uint64 totalFee = 2 * (paymentFee.sourceFee.paymentFee + paymentFee.sourceFee.fixedFee);
		SECTION("Success source is paying")
		{
            paymentFee.sourcePaysForDest = true;
			applyPaymentTx(app, account, dest, accountSeq++, paymentAmount, paymentFee, true);
			REQUIRE(getBalance(account.getPublicKey(), app) == balance - paymentAmount - totalFee);
			REQUIRE(getBalance(dest.getPublicKey(), app) == paymentAmount);
			REQUIRE(getBalance(commissionBalanceID, app) == totalFee);
		}
		SECTION("Success dest is paying")
		{
            paymentFee.sourcePaysForDest = false;
			applyPaymentTx(app, account, dest, accountSeq++, paymentAmount, paymentFee, false);
			REQUIRE(getBalance(account.getPublicKey(), app) == balance - paymentAmount - totalFee / 2);
			REQUIRE(getBalance(dest.getPublicKey(), app) == paymentAmount - totalFee / 2);
			REQUIRE(getBalance(commissionBalanceID, app) == totalFee);
			SECTION("Recipient fee is not required")
			{
				auto commission = getCommissionKP();
				auto payment = createPaymentTx(app.getNetworkID(), commission, commission.getPublicKey(), dest.getPublicKey(), 0, totalFee, getNoPaymentFee(), false);
				payment->getEnvelope().signatures.clear();
				payment->addSignature(root);
				LedgerDelta delta(app.getLedgerManager().getCurrentLedgerHeader(),
					app.getDatabase());
				REQUIRE(applyCheck(payment, delta, app));
				REQUIRE(PaymentOpFrame::getInnerCode(getFirstResult(*payment)) == PaymentResultCode::SUCCESS);
			}
		}

    }
	SECTION("Payment fee with minimum values")
    {
		int64 feeAmount = 1; 
        auto feeFrame = FeeFrame::create(FeeType::PAYMENT_FEE, 0, feeAmount, app.getBaseAsset());
        auto fee = feeFrame->getFee();
        
		applySetFees(app, root, rootSeq++, &fee, false, nullptr);
		auto account = SecretKey::random();
		applyCreateAccountTx(app, root, account, rootSeq++, AccountType::GENERAL);
		auto accountSeq = 1;
		int64 balance = 60 * app.getConfig().EMISSION_UNIT;
		paymentAmount = 1;
		PaymentFeeData paymentFee = getNoPaymentFee();
        paymentFee.sourcePaysForDest = true;
        
		fundAccount(app, root, issuance, rootSeq, account.getPublicKey(), balance);
		auto dest = SecretKey::random();
		applyCreateAccountTx(app, root, dest, rootSeq++, AccountType::GENERAL);

        applyPaymentTx(app, account, dest, accountSeq++, paymentAmount, paymentFee,
                true, "", "", PaymentResultCode::FEE_MISMATCHED);
        paymentFee = getGeneralPaymentFee(0, 1);

		applyPaymentTx(app, account, dest, accountSeq++, paymentAmount, paymentFee,
			true, "");
    }
}

TEST_CASE("single create account SQL", "[singlesql][paymentsql][hide]")
{
    Config::TestDbMode mode = Config::TESTDB_ON_DISK_SQLITE;
#ifdef USE_POSTGRES
    if (!force_sqlite)
        mode = Config::TESTDB_POSTGRESQL;
#endif

    VirtualClock clock;
    Application::pointer app =
        Application::create(clock, getTestConfig(0, mode));
    app->start();

    SecretKey root = getRoot();
    SecretKey a1 = getAccount("A");
    int64_t txfee = app->getLedgerManager().getTxFee();
    const int64_t paymentAmount =
        app->getLedgerManager().getMinBalance(1) + txfee * 10;

    Salt rootSeq = 1;

    {
        auto ctx = app->getDatabase().captureAndLogSQL("createAccount");
        applyCreateAccountTx(*app, root, a1, rootSeq++, AccountType::GENERAL);
    }
}
