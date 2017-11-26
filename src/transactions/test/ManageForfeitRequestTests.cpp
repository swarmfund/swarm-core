// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0
#include "main/Application.h"
#include "ledger/LedgerManager.h"
#include "main/Config.h"
#include "overlay/LoopbackPeer.h"
#include "util/make_unique.h"
#include "main/test.h"
#include "lib/catch.hpp"
#include "util/Logging.h"
#include "TxTests.h"
#include "util/Timer.h"
#include "database/Database.h"
#include "ledger/LedgerDelta.h"
#include "crypto/Hex.h"
#include "crypto/SHA.h"

using namespace stellar;
using namespace stellar::txtest;

typedef std::unique_ptr<Application> appPtr;

TEST_CASE("Manage forfeit request", "[dep_tx][manage_forfeit_request]")
{
    Config const& cfg = getTestConfig(0, Config::TESTDB_POSTGRESQL);

    VirtualClock clock;
    Application::pointer appPtr = Application::create(clock, cfg);
    Application& app = *appPtr;
    app.start();
    closeLedgerOn(app, 2, 1, 7, 2014);
	int64 amount = 10000;

    // set up world
    SecretKey root = getRoot();
	AccountID rootPK = root.getPublicKey();
    Salt rootSeq = 1;

    SecretKey issuance = getIssuanceKey();
    
    SecretKey accountWithMoney = SecretKey::random();
    applyCreateAccountTx(app, root, accountWithMoney, rootSeq++, GENERAL);

    auto asset = app.getBaseAsset();
	SECTION("Malformed")
	{
		SECTION("Negative amount")
		{
			applyManageForfeitRequestTx(app, accountWithMoney,
										accountWithMoney.getPublicKey(), rootSeq++, rootPK, -1, 0, "",
										MANAGE_FORFEIT_REQUEST_INVALID_AMOUNT);
		}
		SECTION("Zero amount")
		{
			applyManageForfeitRequestTx(app, accountWithMoney,
										accountWithMoney.getPublicKey(), rootSeq++, rootPK, 0, 0, "",
										MANAGE_FORFEIT_REQUEST_INVALID_AMOUNT);
		}
		SECTION("Invalid details")
		{
            std::string bigString(app.getConfig().WITHDRAWAL_DETAILS_MAX_LENGTH + 1, 'a');
			applyManageForfeitRequestTx(app, accountWithMoney,
										accountWithMoney.getPublicKey(), rootSeq++, rootPK, 1, 0, bigString,
										MANAGE_FORFEIT_REQUEST_INVALID_DETAILS);
		}
	}
    SECTION("Underfunded")
    {
		applyManageForfeitRequestTx(app, accountWithMoney, accountWithMoney.getPublicKey(), rootSeq++, rootPK, 1, 0, "",
									MANAGE_FORFEIT_REQUEST_UNDERFUNDED);
    }
	SECTION("Reviewer does not exist")
	{
		auto reviewer = SecretKey::random().getPublicKey();
		applyManageForfeitRequestTx(app, accountWithMoney, accountWithMoney.getPublicKey(), rootSeq++, reviewer, 1, 0, "",
									MANAGE_FORFEIT_REQUEST_REVIEWER_NOT_FOUND);
	}
    SECTION("Works for ordinary account")
    {
        auto account = SecretKey::random();
        applyCreateAccountTx(app, root, account, rootSeq++, GENERAL);

		int64 emissionAmount = app.getConfig().EMISSION_UNIT;
		fundAccount(app, root, issuance, rootSeq, account.getPublicKey(), emissionAmount);

		SECTION("Works")
		{
			Salt accSeq = 1;

			applyManageForfeitRequestTx(app, account, account.getPublicKey(), accSeq++, rootPK, 1);
		}
    }

	SECTION("Has Money")
	{
        int64 emissionAmount = app.getConfig().EMISSION_UNIT;
        
        fundAccount(app, root, issuance, rootSeq, accountWithMoney.getPublicKey(), emissionAmount);

        SECTION("Balance mismatch")
        {
			SecretKey account = SecretKey::random();
			applyCreateAccountTx(app, root, account, rootSeq++, GENERAL);
            auto requestID = applyManageForfeitRequestTx(app, accountWithMoney,
														 account.getPublicKey(), rootSeq++, rootPK, 1, 0, "",
														 MANAGE_FORFEIT_REQUEST_BALANCE_MISMATCH);
        }

		SECTION("Can create")
		{
            auto result = applyManageForfeitRequestTx(app, accountWithMoney,
													  accountWithMoney.getPublicKey(), rootSeq++, rootPK, 1);
		}

		SECTION("Can create with reviewer")
		{
			auto reviewer = SecretKey::random();
			applyCreateAccountTx(app, root, reviewer, rootSeq++, GENERAL, nullptr);
			auto reviewerID = reviewer.getPublicKey();
			auto result = applyManageForfeitRequestTx(app, accountWithMoney,
													  accountWithMoney.getPublicKey(), rootSeq++, reviewerID, 1, 0, "",
													  MANAGE_FORFEIT_REQUEST_SUCCESS);
		}
	}

	SECTION("Fee charging")
	{
		SecretKey account = SecretKey::random();
		int64_t initialAmount = 100 * ONE;
		applyCreateAccountTx(app, root, account, rootSeq++, GENERAL);
		fundAccount(app, root, issuance, rootSeq, account.getPublicKey(), initialAmount, app.getBaseAsset());
		rootSeq++;

		auto feeFrame = FeeFrame::create(FORFEIT_FEE, 1, 2 * ONE, app.getBaseAsset());
		auto feeEntry = feeFrame->getFee();
		applySetFees(app, root, rootSeq++, &feeEntry, false, nullptr);

		int64_t amountToForfeit = 2 * ONE;
		int64_t feeToPay = 1 + feeFrame->calculatePercentFee(amountToForfeit);

		SECTION("Fee mismatch")
		{
			int64_t wrongFee = feeToPay + 1;
			applyManageForfeitRequestTx(app, account,
										account.getPublicKey(), rootSeq++, rootPK, amountToForfeit, wrongFee, "",
										MANAGE_FORFEIT_REQUEST_FEE_MISMATCH);
		}

		SECTION("Success")
		{
			applyManageForfeitRequestTx(app, account, account.getPublicKey(), rootSeq++, rootPK, amountToForfeit, feeToPay);

			auto accountBalance = loadBalance(account.getPublicKey(), app, true);
			REQUIRE(accountBalance->getAmount() == initialAmount - amountToForfeit - feeToPay);

		}

		SECTION("Accepted by reviewer")
		{
			auto result = applyManageForfeitRequestTx(app, account, account.getPublicKey(), rootSeq++, rootPK,
													  amountToForfeit, feeToPay);

			auto commissionBalance = loadBalance(app.getCommissionID(), app, true);
			int64_t before = commissionBalance->getAmount();

			applyReviewPaymentRequestTx(app, root, rootSeq++, result.success().paymentID, true);
			commissionBalance = loadBalance(app.getCommissionID(), app, true);
			int64_t after = commissionBalance->getAmount();

			auto accountBalance = loadBalance(account.getPublicKey(), app, true);
			REQUIRE(after == before + feeToPay);
			REQUIRE(accountBalance->getAmount() == initialAmount - amountToForfeit - feeToPay);
		}

	}





}
