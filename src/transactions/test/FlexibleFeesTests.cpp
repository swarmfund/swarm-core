// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the ISC License. See the COPYING file at the top-level directory of
// this distribution or at http://opensource.org/licenses/ISC

#include "main/Application.h"
#include "overlay/LoopbackPeer.h"
#include "util/make_unique.h"
#include "main/test.h"
#include "ledger/FeeHelper.h"
#include "ledger/BalanceHelperLegacy.h"
#include "TxTests.h"

#include "ledger/LedgerDelta.h"
#include "crypto/SHA.h"

#include "test/test_marshaler.h"
#include "ledger/AssetHelperLegacy.h"

using namespace stellar;
using namespace stellar::txtest;

typedef std::unique_ptr<Application> appPtr;


TEST_CASE("Flexible fees", "[dep_tx][flexible_fees]")
{
    Config const& cfg = getTestConfig(0, Config::TESTDB_POSTGRESQL);

    VirtualClock clock;
    Application::pointer appPtr = Application::create(clock, cfg);
    Application& app = *appPtr;

    app.start();

	upgradeToCurrentLedgerVersion(app);

    // set up world
    SecretKey root = getRoot();
    AccountID rootPK = root.getPublicKey();
    SecretKey issuance = getIssuanceKey();
    Salt rootSeq = 1;
	closeLedgerOn(app, 3, 1, 7, 2014);
	applySetFees(app, root, rootSeq, nullptr, false, nullptr);
	closeLedgerOn(app, 4, 1, 7, 2014);
    auto accountType = AccountType::GENERAL;

	auto balanceHelper = BalanceHelperLegacy::Instance();
	auto feeHelper = FeeHelper::Instance();

        std::vector<AssetFrame::pointer> baseAssets;
        AssetHelperLegacy::Instance()->loadBaseAssets(baseAssets, app.getDatabase());
        REQUIRE(!baseAssets.empty());
        auto asset = baseAssets[0];

	SECTION("Set global, set for account check global")
	{
		auto globalFee = createFeeEntry(FeeType::OFFER_FEE, 0, 10 * ONE, asset->getCode(), nullptr, nullptr);
		applySetFees(app, root, rootSeq++, &globalFee, false, nullptr);

		auto a = SecretKey::random();
		auto aPubKey = a.getPublicKey();
		applyCreateAccountTx(app, root, a, rootSeq++, accountType);
		auto accountFee = createFeeEntry(FeeType::OFFER_FEE, 0, 10 * ONE, asset->getCode(), &aPubKey, nullptr);
		applySetFees(app, root, rootSeq++, &accountFee, false, nullptr);

		auto globalFeeFrame = feeHelper->loadFee(FeeType::OFFER_FEE, asset->getCode(), nullptr, nullptr, 0, 0, INT64_MAX, app.getDatabase());
		REQUIRE(globalFeeFrame);
		REQUIRE(globalFeeFrame->getFee() == globalFee);

		auto accountFeeFrame = feeHelper->loadFee(FeeType::OFFER_FEE, asset->getCode(), &aPubKey, nullptr, 0, 0, INT64_MAX, app.getDatabase());
		REQUIRE(accountFeeFrame);
		REQUIRE(accountFeeFrame->getFee() == accountFee);
	}

	SECTION("Custom payment fee for receiver")
    {
		auto account = SecretKey::random();
        auto aPubKey = account.getPublicKey();
		applyCreateAccountTx(app, root, account, rootSeq++, AccountType::GENERAL);
		auto dest = SecretKey::random();
        auto destPubKey = dest.getPublicKey();
		applyCreateAccountTx(app, root, dest, rootSeq++, AccountType::GENERAL);

        auto feeFrame = FeeFrame::create(FeeType::PAYMENT_FEE, 1, 0, asset->getCode());
        auto fee = feeFrame->getFee();
		applySetFees(app, root, rootSeq++, &fee, false, nullptr);


        auto specificFeeFrame = FeeFrame::create(FeeType::PAYMENT_FEE, 0, 10 * ONE, asset->getCode(), &destPubKey);
        auto specificFee = specificFeeFrame->getFee();
        applySetFees(app, root, rootSeq++, &specificFee, false, nullptr);

        
		auto accountSeq = 1;
		int64 balance = 60 * ONE;
		int64 paymentAmount = ONE;
		PaymentFeeData paymentFee = getNoPaymentFee();
        paymentFee.sourcePaysForDest = true;
        
		fundAccount(app, root, issuance, rootSeq, account.getPublicKey(), balance);

        applyPaymentTx(app, account, dest, accountSeq++, paymentAmount, paymentFee,
                true, "", "", PaymentResultCode::FEE_MISMATCHED);
        paymentFee = getGeneralPaymentFee(1, 0);

		applyPaymentTx(app, account, dest, accountSeq++, paymentAmount, paymentFee,
			true, "", "", PaymentResultCode::FEE_MISMATCHED);
            
        paymentFee.destinationFee.paymentFee = paymentAmount * 0.1;
		paymentFee.destinationFee.fixedFee = 0;

		applyPaymentTx(app, account, dest, accountSeq++, paymentAmount, paymentFee,
			true, "", "");
    }

}
