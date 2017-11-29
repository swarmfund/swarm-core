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
#include "transactions/OfferExchange.h"
#include "ledger/LedgerDelta.h"

using namespace stellar;
using namespace stellar::txtest;

typedef std::unique_ptr<Application> appPtr;

// Try setting each option to make sure it works
// try setting all at once
// try setting high threshold ones without the correct sigs
// make sure it doesn't allow us to add signers when we don't have the
// minbalance
TEST_CASE("manage offer", "[dep_tx][offer]")
{
	using xdr::operator==;

	Config const& cfg = getTestConfig(0, Config::TESTDB_POSTGRESQL);

	VirtualClock clock;
	Application::pointer appPtr = Application::create(clock, cfg);
	Application& app = *appPtr;
	app.start();
	closeLedgerOn(app, 2, 1, 7, 2014);

	LedgerDelta delta(app.getLedgerManager().getCurrentLedgerHeader(),
		app.getDatabase());

	Database& db = app.getDatabase();

	// set up world
	SecretKey root = getRoot();
	SecretKey issuance = getIssuanceKey();

	Salt rootSeq = 1;

	AssetCode base = app.getBaseAsset();
	AssetCode quote = app.getStatsQuoteAsset();

	SECTION("Can cancel order even if blocked, but can not create")
	{
		auto quoteAssetAmount = 1000 * ONE;
		for (auto blockReason : xdr::xdr_traits<BlockReasons>::enum_values())
		{
			auto buyer = SecretKey::random();
			applyCreateAccountTx(app, root, buyer, rootSeq, AccountType::GENERAL);
			auto quoteBuyerBalance = BalanceFrame::loadBalance(buyer.getPublicKey(), quote, db, &delta);
			fundAccount(app, root, issuance, rootSeq, quoteBuyerBalance->getBalanceID(), quoteAssetAmount, quote);
			auto baseBuyerBalance = BalanceFrame::loadBalance(buyer.getPublicKey(), base, db, &delta);
			auto orderResult = applyManageOfferTx(app, buyer, rootSeq, 0, baseBuyerBalance->getBalanceID(), quoteBuyerBalance->getBalanceID(), 2, ONE, true);

			// block account
			applyManageAccountTx(app, root, buyer, 0, blockReason);
			// can delete order
			applyManageOfferTx(app, buyer, rootSeq, orderResult.success().offer.offer().offerID, baseBuyerBalance->getBalanceID(), quoteBuyerBalance->getBalanceID(), 0, ONE, true);
			// can't create new one
			auto orderTx = createManageOffer(app.getNetworkID(), buyer, 0, 0, baseBuyerBalance->getBalanceID(), quoteBuyerBalance->getBalanceID(), 2, ONE, true, 0);
			REQUIRE(!applyCheck(orderTx, delta, app));
			auto opResult = getFirstResultCode(*orderTx);
			REQUIRE(opResult == OperationResultCode::opACCOUNT_BLOCKED);
		}
	}
	SECTION("basics")
	{
		auto buyer = SecretKey::random();
		applyCreateAccountTx(app, root, buyer, rootSeq, AccountType::GENERAL);
		auto baseBuyerBalance = BalanceFrame::loadBalance(buyer.getPublicKey(), base, db, &delta);
		REQUIRE(baseBuyerBalance);
		auto quoteBuyerBalance = BalanceFrame::loadBalance(buyer.getPublicKey(), quote, db, &delta);
		REQUIRE(quoteBuyerBalance);
		auto quoteAssetAmount = 1000 * ONE;
		fundAccount(app, root, issuance, rootSeq, quoteBuyerBalance->getBalanceID(), quoteAssetAmount, quote);
		auto seller = SecretKey::random();
		applyCreateAccountTx(app, root, seller, rootSeq, AccountType::GENERAL);
		auto baseSellerBalance = BalanceFrame::loadBalance(seller.getPublicKey(), base, db, &delta);
		REQUIRE(baseBuyerBalance);
		auto quoteSellerBalance = BalanceFrame::loadBalance(seller.getPublicKey(), quote, db, &delta);
		REQUIRE(quoteSellerBalance);
		auto baseAssetAmount = 200 * ONE;
		fundAccount(app, root, issuance, rootSeq, baseSellerBalance->getBalanceID(), baseAssetAmount, base);
		SECTION("Place two offers")
		{
			applyManageOfferTx(app, buyer, rootSeq, 0,
				baseBuyerBalance->getBalanceID(),
				quoteBuyerBalance->getBalanceID(), baseAssetAmount / 2, 5 * ONE, true);
			quoteBuyerBalance = loadBalance(quoteBuyerBalance->getBalanceID(), app);
			REQUIRE(quoteBuyerBalance->getLocked() == quoteAssetAmount / 2);
			REQUIRE(quoteBuyerBalance->getAmount() == quoteAssetAmount / 2);

			applyManageOfferTx(app, buyer, rootSeq, 0,
				baseBuyerBalance->getBalanceID(),
				quoteBuyerBalance->getBalanceID(), baseAssetAmount / 2, 5 * ONE, true);
			quoteBuyerBalance = loadBalance(quoteBuyerBalance->getBalanceID(), app);
			REQUIRE(quoteBuyerBalance->getLocked() == quoteAssetAmount);
			REQUIRE(quoteBuyerBalance->getAmount() == 0);
		}
		SECTION("Place and delete offer")
		{
			auto result = applyManageOfferTx(app, buyer, rootSeq, 0,
                baseBuyerBalance->getBalanceID(),
                quoteBuyerBalance->getBalanceID(), baseAssetAmount, 5 * ONE, true);
			quoteBuyerBalance = loadBalance(quoteBuyerBalance->getBalanceID(), app);
			REQUIRE(quoteBuyerBalance->getLocked() == quoteAssetAmount);
			REQUIRE(quoteBuyerBalance->getAmount() == 0);

			// delete
			applyManageOfferTx(app, buyer, rootSeq, result.success().offer.offer().offerID, baseBuyerBalance->getBalanceID(), quoteBuyerBalance->getBalanceID(), 0, 5 * ONE, true);
			quoteBuyerBalance = loadBalance(quoteBuyerBalance->getBalanceID(), app);
			REQUIRE(quoteBuyerBalance->getLocked() == 0);
			REQUIRE(quoteBuyerBalance->getAmount() == quoteAssetAmount);
		}
		SECTION("base*price = quote")
		{
			auto buyerAccountID = buyer.getPublicKey();
			auto offerFeeFrame = FeeFrame::create(FeeType::OFFER_FEE, 0, int64_t(0.2*ONE), quote, &buyerAccountID);
			auto offerFee = offerFeeFrame->getFee();

			applySetFees(app, root, rootSeq, &offerFee, false, nullptr);

			applyManageOfferTx(app, seller, rootSeq, 0, baseSellerBalance->getBalanceID(), quoteSellerBalance->getBalanceID(), int64_t(0.5*ONE), int64_t(45.11 * ONE), false);
			applyManageOfferTx(app, seller, rootSeq, 0, baseSellerBalance->getBalanceID(), quoteSellerBalance->getBalanceID(), int64_t(0.1*ONE), int64_t(45.12 * ONE), false);
			applyManageOfferTx(app, seller, rootSeq, 0, baseSellerBalance->getBalanceID(), quoteSellerBalance->getBalanceID(), ONE, int64_t(45.13 * ONE), false);

			int64_t fee = 0;
			OfferExchange::setFeeToPay(fee, int64_t(45.13*ONE), int64_t(0.2*ONE));
			auto result = applyManageOfferTx(app, buyer, rootSeq, 0, baseBuyerBalance->getBalanceID(), quoteBuyerBalance->getBalanceID(), ONE, int64_t(45.13 * ONE), true, fee);
			baseBuyerBalance = loadBalance(baseBuyerBalance->getBalanceID(), app);
			for (ClaimOfferAtom claimed : result.success().offersClaimed)
			{
				int64_t expectedQuote = 0;
				REQUIRE(bigDivide(expectedQuote, claimed.baseAmount, claimed.currentPrice, ONE, ROUND_UP));
				REQUIRE(expectedQuote == claimed.quoteAmount);
			}
		}
		SECTION("3 taken by one")
		{
			
			auto buyerAccountID = buyer.getPublicKey();
			auto offerFeeFrame = FeeFrame::create(FeeType::OFFER_FEE, 0, 0.2*ONE, quote, &buyerAccountID);
			auto offerFee = offerFeeFrame->getFee();

			applySetFees(app, root, rootSeq, &offerFee, false, nullptr);

			applyManageOfferTx(app, seller, rootSeq, 0, baseSellerBalance->getBalanceID(), quoteSellerBalance->getBalanceID(), int64_t(0.5*ONE), int64_t(45.11 * ONE), false);
			applyManageOfferTx(app, seller, rootSeq, 0, baseSellerBalance->getBalanceID(), quoteSellerBalance->getBalanceID(), int64_t(0.1*ONE), int64_t(45.12 * ONE), false);
			applyManageOfferTx(app, seller, rootSeq, 0, baseSellerBalance->getBalanceID(), quoteSellerBalance->getBalanceID(), ONE, int64_t(45.13 * ONE), false);

			int64_t fee = 0;
			OfferExchange::setFeeToPay(fee, 45.13*ONE, 0.2*ONE);
			// round up to 2 digits
			fee = bigDivide(fee, 1, 100, ROUND_UP) * 100;
			auto result = applyManageOfferTx(app, buyer, rootSeq, 0, baseBuyerBalance->getBalanceID(), quoteBuyerBalance->getBalanceID(), ONE, int64_t(45.13 * ONE), true, fee);
			baseBuyerBalance = loadBalance(baseBuyerBalance->getBalanceID(), app);
			REQUIRE(baseBuyerBalance->getLocked() == 0);
			REQUIRE(baseBuyerBalance->getAmount() == ONE);
		}
		SECTION("1 to 0.5")
		{
			applyManageOfferTx(app, buyer, rootSeq, 0, baseBuyerBalance->getBalanceID(), quoteBuyerBalance->getBalanceID(), baseAssetAmount, 5 * ONE, true);
			quoteBuyerBalance = loadBalance(quoteBuyerBalance->getBalanceID(), app);
			REQUIRE(quoteBuyerBalance->getLocked() == quoteAssetAmount);
			REQUIRE(quoteBuyerBalance->getAmount() == 0);

			applyManageOfferTx(app, seller, rootSeq, 0, baseSellerBalance->getBalanceID(), quoteSellerBalance->getBalanceID(), baseAssetAmount, 5 * ONE, false);

			quoteBuyerBalance = loadBalance(quoteBuyerBalance->getBalanceID(), app);
			REQUIRE(quoteBuyerBalance->getLocked() == 0);
			REQUIRE(quoteBuyerBalance->getAmount() == 0);

			baseBuyerBalance = loadBalance(baseBuyerBalance->getBalanceID(), app);
			REQUIRE(baseBuyerBalance->getLocked() == 0);
			REQUIRE(baseBuyerBalance->getAmount() == baseAssetAmount);

			baseSellerBalance = loadBalance(baseSellerBalance->getBalanceID(), app);
			REQUIRE(baseSellerBalance->getLocked() == 0);
			REQUIRE(baseSellerBalance->getAmount() == 0);

			quoteSellerBalance = loadBalance(quoteSellerBalance->getBalanceID(), app);
			REQUIRE(quoteSellerBalance->getLocked() == 0);
			REQUIRE(quoteSellerBalance->getAmount() == quoteAssetAmount);
		}
		SECTION("0.5 to 1")
		{

			applyManageOfferTx(app, seller, rootSeq, 0, baseSellerBalance->getBalanceID(), quoteSellerBalance->getBalanceID(), baseAssetAmount, 1 * ONE, false);
			applyManageOfferTx(app, buyer, rootSeq, 0, baseBuyerBalance->getBalanceID(), quoteBuyerBalance->getBalanceID(), baseAssetAmount - 1, 5 * ONE, true);
			int64_t matchAmount = baseAssetAmount - 1;

			quoteBuyerBalance = loadBalance(quoteBuyerBalance->getBalanceID(), app);
			REQUIRE(quoteBuyerBalance->getLocked() == 0);
			int64_t matchQuoteAmount = matchAmount;
			REQUIRE(quoteBuyerBalance->getAmount() == (quoteAssetAmount - matchQuoteAmount));

			baseBuyerBalance = loadBalance(baseBuyerBalance->getBalanceID(), app);
			REQUIRE(baseBuyerBalance->getLocked() == 0);
			REQUIRE(baseBuyerBalance->getAmount() == matchAmount);

			baseSellerBalance = loadBalance(baseSellerBalance->getBalanceID(), app);
			REQUIRE(baseSellerBalance->getLocked() == 1);
			REQUIRE(baseSellerBalance->getAmount() == 0);

			quoteSellerBalance = loadBalance(quoteSellerBalance->getBalanceID(), app);
			REQUIRE(quoteSellerBalance->getLocked() == 0);
			REQUIRE(quoteSellerBalance->getAmount() == matchQuoteAmount);
		}
		SECTION("Physical price restrictions")
		{
			applyManageAssetPairTx(app, root, rootSeq, base, quote, 100 * ONE, 105 * ONE, 0,
								   static_cast<int32_t >(AssetPairPolicy::TRADEABLE) | static_cast<int32_t >(AssetPairPolicy::PHYSICAL_PRICE_RESTRICTION),
								   ManageAssetPairAction::UPDATE_POLICIES);
			auto offerResult = applyManageOfferTx(app, seller, rootSeq, 0, baseSellerBalance->getBalanceID(),
												  quoteSellerBalance->getBalanceID(), baseAssetAmount, 106 * ONE, false);
			auto offer = loadOffer(seller, offerResult.success().offer.offer().offerID, app, true);
			REQUIRE(offer);
			applyManageAssetPairTx(app, root, rootSeq, base, quote, 101 * ONE, 0, 0, 0, ManageAssetPairAction::UPDATE_PRICE);
			// offer was deleted
			offer = loadOffer(seller, offerResult.success().offer.offer().offerID, app, false);
			REQUIRE(!offer);
			// can't place offer
			applyManageOfferTx(app, seller, rootSeq, 0, baseSellerBalance->getBalanceID(),
							   quoteSellerBalance->getBalanceID(), baseAssetAmount, 106 * ONE, false, 0, ManageOfferResultCode::PHYSICAL_PRICE_RESTRICTION);
		}
		SECTION("Current pirce restriction")
		{
			applyManageAssetPairTx(app, root, rootSeq, base, quote, 100 * ONE, 105 * ONE, 5*ONE,
								   static_cast<int32_t >(AssetPairPolicy::TRADEABLE) | static_cast<int32_t >(AssetPairPolicy::CURRENT_PRICE_RESTRICTION),
								   ManageAssetPairAction::UPDATE_POLICIES);
			auto offerResult = applyManageOfferTx(app, seller, rootSeq, 0, baseSellerBalance->getBalanceID(),
												  quoteSellerBalance->getBalanceID(), baseAssetAmount, 95 * ONE, false);
			auto offer = loadOffer(seller, offerResult.success().offer.offer().offerID, app, true);
			REQUIRE(offer);
			applyManageAssetPairTx(app, root, rootSeq, base, quote, 101 * ONE, 0, 0, 0, ManageAssetPairAction::UPDATE_PRICE);
			// offer was not delete deleted
			offer = loadOffer(seller, offerResult.success().offer.offer().offerID, app, false);
			REQUIRE(offer);
			// can't place offer
			applyManageOfferTx(app, seller, rootSeq, 0, baseSellerBalance->getBalanceID(), quoteSellerBalance->getBalanceID(),
							   baseAssetAmount, 95 * ONE, false, 0, ManageOfferResultCode::CURRENT_PRICE_RESTRICTION);
		}
		SECTION("No price restrictions")
		{
			applyManageAssetPairTx(app, root, rootSeq, base, quote, 100 * ONE, 0, 0,
								   static_cast<int32_t >(AssetPairPolicy::TRADEABLE), ManageAssetPairAction::UPDATE_POLICIES);
			auto offerResult = applyManageOfferTx(app, seller, rootSeq, 0, baseSellerBalance->getBalanceID(),
												  quoteSellerBalance->getBalanceID(), baseAssetAmount, 1, false);
			auto offer = loadOffer(seller, offerResult.success().offer.offer().offerID, app, true);
			REQUIRE(offer);
			applyManageAssetPairTx(app, root, rootSeq, base, quote, 101 * ONE, 0, 0, 0, ManageAssetPairAction::UPDATE_PRICE);
			offer = loadOffer(seller, offerResult.success().offer.offer().offerID, app, false);
			REQUIRE(offer);
		}
		SECTION("Asset pair is not tradable")
		{
			applyManageAssetPairTx(app, root, rootSeq, base, quote, 100 * ONE, 0, 0,
								   static_cast<int32_t >(AssetPairPolicy::TRADEABLE) , ManageAssetPairAction::UPDATE_POLICIES);
			auto offerResult = applyManageOfferTx(app, seller, rootSeq, 0, baseSellerBalance->getBalanceID(),
												  quoteSellerBalance->getBalanceID(), baseAssetAmount, 95 * ONE, false);
			auto offer = loadOffer(seller, offerResult.success().offer.offer().offerID, app, true);
			REQUIRE(offer);
			applyManageAssetPairTx(app, root, rootSeq, base, quote, 101 * ONE, 0, 0, 0, ManageAssetPairAction::UPDATE_POLICIES);
			// offer was deleted
			offer = loadOffer(seller, offerResult.success().offer.offer().offerID, app, false);
			REQUIRE(!offer);
			// can place offer
			applyManageOfferTx(app, seller, rootSeq, 0, baseSellerBalance->getBalanceID(),
							   quoteSellerBalance->getBalanceID(), baseAssetAmount, 95 * ONE, false, 0, ManageOfferResultCode::ASSET_PAIR_NOT_TRADABLE);
		}
		SECTION("Seller can receive more then we expected based on base amount and price")
		{
			int64_t sellPrice = int64_t(45.76 * ONE);
			int64_t buyPrice = int64_t(45.77 * ONE);
			applyManageOfferTx(app, buyer, rootSeq, 0, baseBuyerBalance->getBalanceID(),
							   quoteBuyerBalance->getBalanceID(), ONE, buyPrice, true);
			auto offerMatch = applyManageOfferTx(app, seller, rootSeq, 0, baseSellerBalance->getBalanceID(),
												 quoteSellerBalance->getBalanceID(), ONE, sellPrice, false);
			int64_t baseAmount = ONE;
			int64_t quoteAmount = buyPrice;
			REQUIRE(offerMatch.success().offersClaimed[0].baseAmount == baseAmount);
			REQUIRE(offerMatch.success().offersClaimed[0].quoteAmount == quoteAmount);
		}
		SECTION("buy amount == sell amount - buy price")
		{
			int64_t buyAmount = baseAssetAmount;
			int64_t sellAmount = buyAmount;
			int64_t buyPrice = 3 * ONE;
			int64_t sellPrice = ONE;
			applyManageOfferTx(app, buyer, rootSeq, 0, baseBuyerBalance->getBalanceID(),
							   quoteBuyerBalance->getBalanceID(), buyAmount, buyPrice, true);
			auto offerMatch = applyManageOfferTx(app, seller, rootSeq, 0, baseSellerBalance->getBalanceID(),
												 quoteSellerBalance->getBalanceID(), sellAmount, sellPrice, false);
			REQUIRE(offerMatch.success().offersClaimed[0].currentPrice == buyPrice);
		}
		SECTION("buy amount > sell amount - buy price")
		{
			int64_t buyAmount = baseAssetAmount;
			int64_t sellAmount = buyAmount - 1;
			int64_t buyPrice = 3 * ONE;
			int64_t sellPrice = ONE;
			applyManageOfferTx(app, buyer, rootSeq, 0, baseBuyerBalance->getBalanceID(),
							   quoteBuyerBalance->getBalanceID(), buyAmount, buyPrice, true);
			auto offerMatch = applyManageOfferTx(app, seller, rootSeq, 0, baseSellerBalance->getBalanceID(),
												 quoteSellerBalance->getBalanceID(), sellAmount, sellPrice, false);
			REQUIRE(offerMatch.success().offersClaimed[0].currentPrice == buyPrice);
		}
		SECTION("buy amount < sell amount - sell price")
		{
			int64_t sellAmount = baseAssetAmount;
			int64_t buyAmount = sellAmount - 1;
			int64_t buyPrice = 3 * ONE;
			int64_t sellPrice = ONE;
			applyManageOfferTx(app, seller, rootSeq, 0, baseSellerBalance->getBalanceID(),
							   quoteSellerBalance->getBalanceID(), sellAmount, sellPrice, false);
			auto offerMatch = applyManageOfferTx(app, buyer, rootSeq, 0, baseBuyerBalance->getBalanceID(),
												 quoteBuyerBalance->getBalanceID(), buyAmount, buyPrice, true);
			REQUIRE(offerMatch.success().offersClaimed[0].currentPrice == sellPrice);
		}
		SECTION("Offer fees")
		{
            auto offerFeeFrame = FeeFrame::create(FeeType::OFFER_FEE, 0, ONE, quote);
            auto offerFee = offerFeeFrame->getFee();
            
			applySetFees(app, root, rootSeq, &offerFee, false, nullptr);
			SECTION("Try to spend all quote")
			{
				int64_t offerPrice = 5 * ONE;
				int64_t feeToPay = 0;
				REQUIRE(OfferExchange::setFeeToPay(feeToPay, quoteAssetAmount, ONE));
				applyManageOfferTx(app, buyer, rootSeq, 0,
					baseBuyerBalance->getBalanceID(),
					quoteBuyerBalance->getBalanceID(), quoteAssetAmount/offerPrice*ONE, offerPrice, true, feeToPay,
                    ManageOfferResultCode::UNDERFUNDED);
			}
			SECTION("Not 0 percent fee")
			{
				applyManageOfferTx(app, seller, rootSeq, 0, baseSellerBalance->getBalanceID(),
								   quoteSellerBalance->getBalanceID(), baseAssetAmount, 1 * ONE, false, 0, ManageOfferResultCode::MALFORMED);
			}
			SECTION("Success")
			{
				auto buyerAccountID = baseBuyerBalance->getAccountID();
				offerFeeFrame = FeeFrame::create(FeeType::OFFER_FEE, 0, 2 * ONE, quote, &buyerAccountID);
				offerFee = offerFeeFrame->getFee();
				applySetFees(app, root, rootSeq, &offerFee, false, nullptr);
				int64_t matchAmount = baseAssetAmount;
				int64_t matchPrice = 4 * ONE;
				int64_t quoteAssetMatchAmount = matchAmount * matchPrice/ONE;

				int64_t sellerMatchFee, buyerOfferFeeToLock, sellerOfferFee;
				REQUIRE(OfferExchange::setFeeToPay(sellerMatchFee, quoteAssetMatchAmount, ONE));
				REQUIRE(OfferExchange::setFeeToPay(sellerOfferFee, baseAssetAmount, ONE));
				REQUIRE(OfferExchange::setFeeToPay(buyerOfferFeeToLock, baseAssetAmount * 4, offerFee.percentFee));
				applyManageOfferTx(app, seller, rootSeq, 0, baseSellerBalance->getBalanceID(), quoteSellerBalance->getBalanceID(), baseAssetAmount, ONE, false, sellerOfferFee);
				auto offerMatch = applyManageOfferTx(app, buyer, rootSeq, 0, baseBuyerBalance->getBalanceID(), quoteBuyerBalance->getBalanceID(), baseAssetAmount, matchPrice, true, buyerOfferFeeToLock);
				REQUIRE(offerMatch.success().offersClaimed[0].quoteAmount == quoteAssetMatchAmount);
				REQUIRE(offerMatch.success().offersClaimed[0].bFeePaid == sellerMatchFee);
				int64_t buyerOfferFee = 0;
				REQUIRE(OfferExchange::setFeeToPay(buyerOfferFee, quoteAssetMatchAmount, offerFee.percentFee));
				REQUIRE(offerMatch.success().offersClaimed[0].aFeePaid == buyerOfferFee);
				REQUIRE(offerMatch.success().offersClaimed.size() == 1);

				quoteBuyerBalance = loadBalance(quoteBuyerBalance->getBalanceID(), app);
				REQUIRE(quoteBuyerBalance->getLocked() == 0);
				REQUIRE(quoteBuyerBalance->getAmount() == (quoteAssetAmount - quoteAssetMatchAmount - buyerOfferFee));

				baseBuyerBalance = loadBalance(baseBuyerBalance->getBalanceID(), app);
				REQUIRE(baseBuyerBalance->getLocked() == 0);
				REQUIRE(baseBuyerBalance->getAmount() == baseAssetAmount);

				baseSellerBalance = loadBalance(baseSellerBalance->getBalanceID(), app);
				REQUIRE(baseSellerBalance->getLocked() == 0);
				REQUIRE(baseSellerBalance->getAmount() == 0);

				quoteSellerBalance = loadBalance(quoteSellerBalance->getBalanceID(), app);
				REQUIRE(quoteSellerBalance->getLocked() == 0);
				REQUIRE(quoteSellerBalance->getAmount() == quoteAssetMatchAmount - sellerMatchFee);

				auto commissionQuoteBalance = BalanceFrame::loadBalance(app.getCommissionID(), quote, db, &delta);
				REQUIRE(commissionQuoteBalance);
				REQUIRE(commissionQuoteBalance->getAmount() == buyerOfferFee + sellerMatchFee);
			}
		}
	}
	SECTION("Random tests")
	{

		int64_t fee = int64_t(0.75 * ONE);
        auto offerFeeFrame = FeeFrame::create(FeeType::OFFER_FEE, 0, fee, quote);
        auto offerFee = offerFeeFrame->getFee();
		applySetFees(app, root, rootSeq, &offerFee, false, nullptr);

		auto buyer = SecretKey::random();
		applyCreateAccountTx(app, root, buyer, rootSeq, AccountType::GENERAL);
		auto baseBuyerBalance = BalanceFrame::loadBalance(buyer.getPublicKey(), base, db, &delta);
		REQUIRE(baseBuyerBalance);
		auto quoteBuyerBalance = BalanceFrame::loadBalance(buyer.getPublicKey(), quote, db, &delta);
		REQUIRE(quoteBuyerBalance);
		auto quoteAssetAmount = 10000 * ONE;
		fundAccount(app, root, issuance, rootSeq, quoteBuyerBalance->getBalanceID(), quoteAssetAmount, quote);

		auto seller = SecretKey::random();
		applyCreateAccountTx(app, root, seller, rootSeq, AccountType::GENERAL);
		auto baseSellerBalance = BalanceFrame::loadBalance(seller.getPublicKey(), base, db, &delta);
		REQUIRE(baseSellerBalance);
		auto quoteSellerBalance = BalanceFrame::loadBalance(seller.getPublicKey(), quote, db, &delta);
		REQUIRE(quoteSellerBalance);
		auto baseAssetAmount = 1000 * ONE;
		fundAccount(app, root, issuance, rootSeq, baseSellerBalance->getBalanceID(), baseAssetAmount, base);

		auto randomPrice = []() {
			// to ensure that quote amount is >= 1
			return (rand() % INT16_MAX + ONE);
		};

		auto randomAmount = []() {
			// to ensure that base amount > 1;
			return (rand() % INT16_MAX + 1);
		};

		int64_t matchesLeft = 1000;

		// buyer and seller are placing ramdom orders untill one of them runs out of money
		while (matchesLeft > 0)
		{
			int64_t buyerAmount = randomAmount();
			int64_t buyerPrice = randomPrice();
			quoteBuyerBalance = BalanceFrame::loadBalance(buyer.getPublicKey(), quote, db, &delta);
			int64_t buyerOfferAmount = buyerAmount*buyerPrice;
			assert(buyerOfferAmount > 0);


			int64_t buyerFee = 0;
			REQUIRE(OfferExchange::setFeeToPay(buyerFee, bigDivide(buyerPrice, buyerAmount, int64_t(ONE), ROUND_UP), fee));

			if (quoteBuyerBalance->getAmount() < buyerOfferAmount + buyerFee)
			{
				quoteAssetAmount += buyerOfferAmount + buyerFee;
				assert(quoteAssetAmount > 0);
				fundAccount(app, root, issuance, rootSeq, quoteBuyerBalance->getBalanceID(), buyerOfferAmount + buyerFee, quote);
			}

			auto offerResult = applyManageOfferTx(app, buyer, rootSeq, 0, baseBuyerBalance->getBalanceID(),
												  quoteBuyerBalance->getBalanceID(), buyerAmount, buyerPrice, true, buyerFee);
			matchesLeft -= offerResult.success().offersClaimed.size();

			int64_t sellerAmount = randomAmount();
			baseSellerBalance = BalanceFrame::loadBalance(seller.getPublicKey(), base, db, &delta);
			if (baseSellerBalance->getAmount() < sellerAmount)
			{
				baseAssetAmount += sellerAmount;
				assert(baseAssetAmount > 0);
				fundAccount(app, root, issuance, rootSeq, baseSellerBalance->getBalanceID(), sellerAmount, base);
			}

			// to ensure that quote amount is >= 1
			int64_t sellerPrice = randomPrice();
			int64_t sellerFee = 0;
			REQUIRE(OfferExchange::setFeeToPay(sellerFee, bigDivide(sellerAmount, sellerPrice, int64_t(ONE), ROUND_UP), fee));
			offerResult = applyManageOfferTx(app, seller, rootSeq, 0, baseSellerBalance->getBalanceID(),
											 quoteSellerBalance->getBalanceID(), sellerAmount, sellerPrice, false, sellerFee);
			matchesLeft -= offerResult.success().offersClaimed.size();
			LOG(INFO) << "matches left: " << matchesLeft;
		}

		auto offersByAccount = OfferFrame::loadAllOffers(app.getDatabase());
		// delete all buyers offers
		auto buyersOffers = offersByAccount[buyer.getPublicKey()];
		for (auto buyerOffer : buyersOffers)
		{
			applyManageOfferTx(app, buyer, rootSeq, buyerOffer->getOfferID(), baseBuyerBalance->getBalanceID(),
							   quoteBuyerBalance->getBalanceID(), 0, 1, true);
		}

		// delete all seller offers
		auto sellerOffers = offersByAccount[seller.getPublicKey()];
		for (auto sellerOffer : sellerOffers)
		{
			applyManageOfferTx(app, seller, rootSeq, sellerOffer->getOfferID(), baseSellerBalance->getBalanceID(),
							   quoteSellerBalance->getBalanceID(), 0, 1, false);
		}

		offersByAccount = OfferFrame::loadAllOffers(app.getDatabase());
		REQUIRE(offersByAccount.size() == 0);

		baseBuyerBalance = BalanceFrame::loadBalance(buyer.getPublicKey(), base, db, &delta);
		// there must be matches
		REQUIRE(baseBuyerBalance->getAmount() != 0);
		REQUIRE(baseBuyerBalance->getLocked() == 0);

		quoteBuyerBalance = BalanceFrame::loadBalance(buyer.getPublicKey(), quote, db, &delta);
		REQUIRE(quoteBuyerBalance->getLocked() == 0);

		baseSellerBalance = BalanceFrame::loadBalance(seller.getPublicKey(), base, db, &delta);
		REQUIRE(baseSellerBalance->getLocked() == 0);

		quoteSellerBalance = BalanceFrame::loadBalance(seller.getPublicKey(), quote, db, &delta);
		// there must be matches
		REQUIRE(quoteSellerBalance->getAmount() != 0);
		REQUIRE(quoteSellerBalance->getLocked() == 0);

		REQUIRE(baseBuyerBalance->getAmount() + baseSellerBalance->getAmount() == baseAssetAmount);

		auto comissionAccount = BalanceFrame::loadBalance(app.getCommissionID(), quote, db, nullptr);
		REQUIRE(comissionAccount);
		REQUIRE(comissionAccount->getAmount() + quoteBuyerBalance->getAmount() + quoteSellerBalance->getAmount() == quoteAssetAmount);
	}
}
