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
#include "crypto/SHA.h"

using namespace stellar;
using namespace stellar::txtest;

typedef std::unique_ptr<Application> appPtr;

TEST_CASE("direct_debit", "[tx][direct_debit]")
{
    Config const& cfg = getTestConfig(0, Config::TESTDB_POSTGRESQL);

    VirtualClock clock;
    Application::pointer appPtr = Application::create(clock, cfg);
    Application& app = *appPtr;
    app.start();
    closeLedgerOn(app, 2, 1, 7, 2014);
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
    applyCreateAccountTx(app, root, aWM, rootSeq++, GENERAL);
    fundAccount(app, root, issuance, rootSeq, aWM.getPublicKey(), emissionAmount);

    auto secondAsset = "AETH";

	SECTION("basic tests")
	{
		auto account = SecretKey::random();
		applyCreateAccountTx(app, root, account, rootSeq++, GENERAL);
		REQUIRE(getBalance(account.getPublicKey(), app) == 0);
		REQUIRE(getBalance(aWM.getPublicKey(), app) ==
            emissionAmount);

        PaymentOp paymentOp;
        paymentOp.amount = emissionAmount / 2;
        paymentOp.feeData.sourceFee.paymentFee = 0;
        paymentOp.feeData.sourceFee.fixedFee = 0;
        paymentOp.subject = "123";
        paymentOp.sourceBalanceID = aWM.getPublicKey();
        paymentOp.destinationBalanceID = account.getPublicKey();
        paymentOp.reference = "";
    
        auto paymentResult = applyDirectDebitTx(app, account, 0, aWM.getPublicKey(),
            paymentOp, DIRECT_DEBIT_NO_TRUST);  

        TrustData trustData;
        TrustEntry trust;
        trust.allowedAccount = account.getPublicKey();
        trust.balanceToUse = aWM.getPublicKey();
        trustData.trust = trust;
        trustData.action = TRUST_ADD;
        applySetOptions(app, aWM, rootSeq++, nullptr, nullptr, &trustData);

        applyDirectDebitTx(app, account, rootSeq++, account.getPublicKey(),
            paymentOp, DIRECT_DEBIT_MALFORMED);

		SECTION("Balance mismatched")
		{
			paymentOp.sourceBalanceID = aWM.getPublicKey();
			auto randomAccount = SecretKey::random();
			applyCreateAccountTx(app, root, randomAccount, 0, GENERAL);
			applyDirectDebitTx(app, account, rootSeq++, randomAccount.getPublicKey(),
				paymentOp, DIRECT_DEBIT_BALANCE_ACCOUNT_MISMATCHED);
		}


		SECTION("Success")
		{
			paymentResult = applyDirectDebitTx(app, account, rootSeq++, aWM.getPublicKey(),
				paymentOp);

			REQUIRE(getBalance(account.getPublicKey(), app) == paymentAmount);
			REQUIRE(getBalance(aWM.getPublicKey(), app) == (emissionAmount - paymentAmount));
			REQUIRE(paymentResult.success().paymentResponse.destination == account.getPublicKey());
			REQUIRE(paymentResult.success().paymentResponse.asset == app.getBaseAsset());
	   }
	}
}
