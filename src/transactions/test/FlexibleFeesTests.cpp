// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the ISC License. See the COPYING file at the top-level directory of
// this distribution or at http://opensource.org/licenses/ISC

#include "main/Application.h"
#include "overlay/LoopbackPeer.h"
#include "util/make_unique.h"
#include "main/test.h"
#include "util/types.h"
#include "lib/catch.hpp"
#include "ledger/FeeHelper.h"
#include "ledger/BalanceHelper.h"

#include "TxTests.h"
#include "ledger/LedgerDelta.h"

#include "crypto/SHA.h"

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

	auto balanceHelper = BalanceHelper::Instance();
	auto feeHelper = FeeHelper::Instance();

	SECTION("Set global, set for account check global")
	{
		auto globalFee = createFeeEntry(FeeType::OFFER_FEE, 0, 10 * ONE, app.getBaseAsset(), nullptr, nullptr);
		applySetFees(app, root, rootSeq++, &globalFee, false, nullptr);

		auto a = SecretKey::random();
		auto aPubKey = a.getPublicKey();
		applyCreateAccountTx(app, root, a, rootSeq++, accountType);
		auto accountFee = createFeeEntry(FeeType::OFFER_FEE, 0, 10 * ONE, app.getBaseAsset(), &aPubKey, nullptr);
		applySetFees(app, root, rootSeq++, &accountFee, false, nullptr);

		auto globalFeeFrame = feeHelper->loadFee(FeeType::OFFER_FEE, app.getBaseAsset(), nullptr, nullptr, 0, 0, INT64_MAX, app.getDatabase());
		REQUIRE(globalFeeFrame);
		REQUIRE(globalFeeFrame->getFee() == globalFee);

		auto accountFeeFrame = feeHelper->loadFee(FeeType::OFFER_FEE, app.getBaseAsset(), &aPubKey, nullptr, 0, 0, INT64_MAX, app.getDatabase());
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

        auto feeFrame = FeeFrame::create(FeeType::PAYMENT_FEE, 1, 0, app.getBaseAsset());
        auto fee = feeFrame->getFee();
		applySetFees(app, root, rootSeq++, &fee, false, nullptr);


        auto specificFeeFrame = FeeFrame::create(FeeType::PAYMENT_FEE, 0, 10 * ONE, app.getBaseAsset(), &destPubKey);
        auto specificFee = specificFeeFrame->getFee();
        applySetFees(app, root, rootSeq++, &specificFee, false, nullptr);

        
		auto accountSeq = 1;
		int64 balance = 60 * app.getConfig().EMISSION_UNIT;
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

	SECTION("Custom offer fee for seller")
    {
        Database& db = app.getDatabase();


        AssetCode base = app.getBaseAsset();
        AssetCode quote = app.getStatsQuoteAsset();

		auto buyer = SecretKey::random();
        auto buyerPubKey = buyer.getPublicKey();
		applyCreateAccountTx(app, root, buyer, rootSeq, AccountType::GENERAL);
		auto baseBuyerBalance = balanceHelper->loadBalance(buyer.getPublicKey(), base, db, nullptr);
		REQUIRE(baseBuyerBalance);
		auto quoteBuyerBalance = balanceHelper->loadBalance(buyer.getPublicKey(), quote, db, nullptr);
		REQUIRE(quoteBuyerBalance);
		auto quoteAssetAmount = 1000 * ONE;
		fundAccount(app, root, issuance, rootSeq, quoteBuyerBalance->getBalanceID(), quoteAssetAmount, quote);
		auto seller = SecretKey::random();
        auto sellerPubKey = seller.getPublicKey();
		applyCreateAccountTx(app, root, seller, rootSeq, AccountType::GENERAL);
		auto baseSellerBalance = balanceHelper->loadBalance(sellerPubKey, base, db, nullptr);
		REQUIRE(baseBuyerBalance);
		auto quoteSellerBalance = balanceHelper->loadBalance(sellerPubKey, quote, db, nullptr);
		REQUIRE(quoteSellerBalance);
		auto baseAssetAmount = 200 * ONE;
		fundAccount(app, root, issuance, rootSeq, baseSellerBalance->getBalanceID(), baseAssetAmount, base);

        auto offerFeeFrame = FeeFrame::create(FeeType::OFFER_FEE, 0, ONE, quote);
        auto offerFee = offerFeeFrame->getFee();
        applySetFees(app, root, rootSeq, &offerFee, false, nullptr);

        auto sellerOfferFeeFrame = FeeFrame::create(FeeType::OFFER_FEE, 0, 30 * ONE, quote, &sellerPubKey);
        auto sellerOfferFee = sellerOfferFeeFrame->getFee();
            
        applySetFees(app, root, rootSeq, &sellerOfferFee, false, nullptr);
        SECTION("Success")
        {
			int64_t sellerFee = bigDivide(baseAssetAmount, sellerOfferFee.percentFee, 100 * ONE, ROUND_UP);
            applyManageOfferTx(app, seller, rootSeq, 0, baseSellerBalance->getBalanceID(), quoteSellerBalance->getBalanceID(), baseAssetAmount, 1 * ONE, false, sellerFee);
            int64_t matchAmount = baseAssetAmount;
            int64_t buyerQuoteAmount = bigDivide(baseAssetAmount, 4*ONE, ONE, ROUND_UP);
            int64_t buyerOfferFee = bigDivide(buyerQuoteAmount, ONE, 100*ONE, ROUND_UP);

            auto offerMatch = applyManageOfferTx(app, buyer, rootSeq, 0, baseBuyerBalance->getBalanceID(), quoteBuyerBalance->getBalanceID(), baseAssetAmount, 4 * ONE, true,
                buyerOfferFee);
			int64_t quoteMatchAmount = 4 * matchAmount;
            int64_t buyerExpectedFee = bigDivide(quoteMatchAmount, offerFee.percentFee, int64_t(ONE * 100), ROUND_UP);
            REQUIRE(offerMatch.success().offersClaimed[0].quoteAmount == quoteMatchAmount);
			int64_t bFeePaid = offerMatch.success().offersClaimed[0].bFeePaid;
			int64_t aFeePaid = offerMatch.success().offersClaimed[0].aFeePaid;
            REQUIRE(aFeePaid == buyerExpectedFee);
            REQUIRE(offerMatch.success().offersClaimed.size() == 1);
			int64_t sellerExpectedFee = bigDivide(quoteMatchAmount, sellerOfferFee.percentFee, int64_t(ONE * 100), ROUND_UP);
            REQUIRE(bFeePaid == sellerExpectedFee);

            quoteBuyerBalance = loadBalance(quoteBuyerBalance->getBalanceID(), app);
            REQUIRE(quoteBuyerBalance->getLocked() == 0);
            REQUIRE(quoteBuyerBalance->getAmount() == (quoteAssetAmount - quoteMatchAmount - buyerExpectedFee));
            baseBuyerBalance = loadBalance(baseBuyerBalance->getBalanceID(), app);
            REQUIRE(baseBuyerBalance->getLocked() == 0);
            REQUIRE(baseBuyerBalance->getAmount() == baseAssetAmount);

            baseSellerBalance = loadBalance(baseSellerBalance->getBalanceID(), app);
            REQUIRE(baseSellerBalance->getLocked() == 0);
            REQUIRE(baseSellerBalance->getAmount() == 0);

            quoteSellerBalance = loadBalance(quoteSellerBalance->getBalanceID(), app);
            REQUIRE(quoteSellerBalance->getLocked() == 0);
            REQUIRE(quoteSellerBalance->getAmount() == quoteMatchAmount - sellerExpectedFee);

            auto commissionQuoteBalance = balanceHelper->loadBalance(app.getCommissionID(), quote, db, nullptr);
            REQUIRE(commissionQuoteBalance);
            REQUIRE(commissionQuoteBalance->getAmount() == sellerExpectedFee + buyerExpectedFee);
        }


    }

	SECTION("Custom forfeit fee")
    {
		auto account2 = SecretKey::random();
        auto account2PubKey = account2.getPublicKey();
		applyCreateAccountTx(app, root, account2, rootSeq++, AccountType::GENERAL);
        auto specificFeeFrame = FeeFrame::create(FeeType::FORFEIT_FEE, 25, 0, app.getBaseAsset(), &account2PubKey, nullptr, FeeFrame::SUBTYPE_ANY);
        auto specificFee = specificFeeFrame->getFee();
        applySetFees(app, root, rootSeq++, &specificFee, false, nullptr);

        uint64_t amountToForfeit = 10 * ONE;
        int64_t balanceAmount = amountToForfeit * 10;

        fundAccount(app, root, issuance, rootSeq, account2PubKey, balanceAmount);

        closeLedgerOn(app, 5, 2, 7, 2014);
        applyManageForfeitRequestTx(app, account2,
                                    account2PubKey, rootSeq++, rootPK, amountToForfeit, 0, "",
                                    ManageForfeitRequestResultCode::FEE_MISMATCH);

        applyManageForfeitRequestTx(app, account2, account2PubKey, rootSeq++, rootPK, amountToForfeit, 25);
        
        auto balance2 = loadBalance(account2PubKey, app, true);
        REQUIRE(balance2->getAmount() == (balanceAmount - amountToForfeit - 25));
    }

	SECTION("Custom forfeit fee (fixed and percent)")
    {
		auto account2 = SecretKey::random();
        auto account2PubKey = account2.getPublicKey();
		applyCreateAccountTx(app, root, account2, rootSeq++, AccountType::GENERAL);

		const int64_t amount = 100 * ONE;
        auto feeFrame = FeeFrame::create(FeeType::FORFEIT_FEE, 1, 5 * ONE, app.getBaseAsset(), nullptr, nullptr, FeeFrame::SUBTYPE_ANY);

		auto feeToSet = feeFrame->getFee();
        applySetFees(app, root, rootSeq++, &feeToSet, false, nullptr);

        int64_t expectedPercentFee = feeFrame->calculatePercentFee(9 * amount);
        int64_t expectedFixedFee = feeFrame->getFee().fixedFee;
        int64_t totalFee = expectedFixedFee + expectedPercentFee;

        int64_t amountToForfeit = 9 * amount;
        int64_t balanceAmount = amountToForfeit * 10;

        fundAccount(app, root, issuance, rootSeq, account2.getPublicKey(), balanceAmount);
        auto result = applyManageForfeitRequestTx(app, account2, account2PubKey, rootSeq++, rootPK, amountToForfeit, totalFee);
        REQUIRE(result.code() == ManageForfeitRequestResultCode::SUCCESS);

        auto balance2 = loadBalance(account2PubKey, app, true);
        REQUIRE(balance2->getAmount() == (balanceAmount - amountToForfeit - expectedPercentFee - expectedFixedFee));
    }


}
