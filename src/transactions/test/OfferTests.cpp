// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "main/Application.h"
#include "main/Config.h"
#include "util/Timer.h"
#include "overlay/LoopbackPeer.h"
#include "main/test.h"
#include "TxTests.h"
#include "transactions/dex/OfferExchange.h"
#include "ledger/LedgerDeltaImpl.h"
#include "ledger/BalanceHelperLegacy.h"
#include "ledger/OfferHelper.h"
#include "test_helper/ManageAssetTestHelper.h"
#include "test_helper/IssuanceRequestHelper.h"
#include "test_helper/ManageAssetPairTestHelper.h"
#include "test_helper/ManageOfferTestHelper.h"
#include "test/test_marshaler.h"

using namespace stellar;
using namespace stellar::txtest;

typedef std::unique_ptr<Application> appPtr;

// Try setting each option to make sure it works
// try setting all at once
// try setting high threshold ones without the correct sigs
// make sure it doesn't allow us to add signers when we don't have the
// minbalance
TEST_CASE("manage offer", "[tx][offer]")
{
    using xdr::operator==;

    Config const& cfg = getTestConfig(0, Config::TESTDB_POSTGRESQL);
    VirtualClock clock;
    Application::pointer appPtr = Application::create(clock, cfg);
    Application& app = *appPtr;
    app.start();
    auto testManager = TestManager::make(app);
    auto& db = testManager->getDB();
    LedgerDeltaImpl delta(testManager->getLedgerManager().getCurrentLedgerHeader(),
                          testManager->getDB());

    // set up world
    SecretKey root = getRoot();
    auto rootAccount = Account{ root, 0 };

    Salt rootSeq = 1;

    auto assetTestHelper = ManageAssetTestHelper(testManager);
    AssetCode base = "BTC";
    assetTestHelper.createAsset(rootAccount, rootAccount.key, base, rootAccount, int32(AssetPolicy::BASE_ASSET));
    AssetCode quote = "USD";
    assetTestHelper.createAsset(rootAccount, rootAccount.key, quote, rootAccount,int32(AssetPolicy::BASE_ASSET));

    auto assetPairHelper = ManageAssetPairTestHelper(testManager);
    assetPairHelper.applyManageAssetPairTx(rootAccount, base, quote, 1, 0, 0, int32(AssetPairPolicy::TRADEABLE_SECONDARY_MARKET));

    auto issuanceHelper = IssuanceRequestHelper(testManager);
    auto fundAccount = [&issuanceHelper, &rootAccount](AssetCode code, uint64_t amount, BalanceID receiver)
    {
        issuanceHelper.authorizePreIssuedAmount(rootAccount, rootAccount.key, code, amount, rootAccount);
        issuanceHelper.applyCreateIssuanceRequest(rootAccount, code, amount, receiver, SecretKey::random().getStrKeyPublic());
    };

    auto balanceHelper = BalanceHelperLegacy::Instance();
    auto offerHelper = OfferHelper::Instance();

    auto offerTestHelper = ManageOfferTestHelper(testManager);

    SECTION("Can cancel order even if blocked, but can not create")
    {
        auto quoteAssetAmount = 1000 * ONE;
        for (auto blockReason : xdr::xdr_traits<BlockReasons>::enum_values())
        {
            auto buyer = Account{ SecretKey::random(), 0 };
            applyCreateAccountTx(app, root, buyer.key, rootSeq,
                                 AccountType::GENERAL);
            auto quoteBuyerBalance = balanceHelper->
                loadBalance(buyer.key.getPublicKey(), quote, db, &delta);
            fundAccount(quote, quoteAssetAmount, quoteBuyerBalance->getBalanceID());
            auto baseBuyerBalance = balanceHelper->
                loadBalance(buyer.key.getPublicKey(), base, db, &delta);
            auto orderResult = offerTestHelper.applyManageOffer(buyer, 0,
                                                  baseBuyerBalance->
                                                  getBalanceID(),
                                                  quoteBuyerBalance->
                                                  getBalanceID(), 2, ONE, true, 0);

            // block account
            applyManageAccountTx(app, root, buyer.key, 0, blockReason);
            // can delete order
            offerTestHelper.applyManageOffer(buyer,
                               orderResult.success().offer.offer().offerID,
                               baseBuyerBalance->getBalanceID(),
                               quoteBuyerBalance->getBalanceID(), 0, ONE, true, 0);
            // can't create new one
            auto orderTx = offerTestHelper.creatManageOfferTx(buyer, 0,
                                             baseBuyerBalance->getBalanceID(),
                                             quoteBuyerBalance->getBalanceID(),
                                             2, ONE, true, 0);
            REQUIRE(!applyCheck(orderTx, delta, app));
            auto opResult = getFirstResultCode(*orderTx);
            REQUIRE(opResult == OperationResultCode::opACCOUNT_BLOCKED);
        }
    }
    SECTION("basics")
    {
        auto buyer = Account{ SecretKey::random() , 0};
        applyCreateAccountTx(app, root, buyer.key, rootSeq, AccountType::GENERAL);
        auto baseBuyerBalance = balanceHelper->loadBalance(buyer.key.getPublicKey(),
                                                           base, db, &delta);
        REQUIRE(baseBuyerBalance);
        auto quoteBuyerBalance = balanceHelper->
            loadBalance(buyer.key.getPublicKey(), quote, db, &delta);
        REQUIRE(quoteBuyerBalance);
        auto quoteAssetAmount = 1000 * ONE;
        fundAccount(quote, quoteAssetAmount, quoteBuyerBalance->getBalanceID());
        auto seller = Account{ SecretKey::random() , 0};
        applyCreateAccountTx(app, root, seller.key, rootSeq, AccountType::GENERAL);
        auto baseSellerBalance = balanceHelper->
            loadBalance(seller.key.getPublicKey(), base, db, &delta);
        REQUIRE(baseBuyerBalance);
        auto quoteSellerBalance = balanceHelper->
            loadBalance(seller.key.getPublicKey(), quote, db, &delta);
        REQUIRE(quoteSellerBalance);
        auto baseAssetAmount = 200 * ONE;
        fundAccount(base, baseAssetAmount, baseSellerBalance->getBalanceID());
        SECTION("Place two offers")
        {
            offerTestHelper.applyManageOffer(buyer, 0,
                               baseBuyerBalance->getBalanceID(),
                               quoteBuyerBalance->getBalanceID(),
                               baseAssetAmount / 2, 5 * ONE, true, 0);
            quoteBuyerBalance = loadBalance(quoteBuyerBalance->getBalanceID(),
                                            app);
            REQUIRE(quoteBuyerBalance->getLocked() == quoteAssetAmount / 2);
            REQUIRE(quoteBuyerBalance->getAmount() == quoteAssetAmount / 2);

            offerTestHelper.applyManageOffer(buyer, 0,
                               baseBuyerBalance->getBalanceID(),
                               quoteBuyerBalance->getBalanceID(),
                               baseAssetAmount / 2, 5 * ONE, true, 0);
            quoteBuyerBalance = loadBalance(quoteBuyerBalance->getBalanceID(),
                                            app);
            REQUIRE(quoteBuyerBalance->getLocked() == quoteAssetAmount);
            REQUIRE(quoteBuyerBalance->getAmount() == 0);
        }
        SECTION("Place and delete offer")
        {
            auto result = offerTestHelper.applyManageOffer(buyer, 0,
                                             baseBuyerBalance->getBalanceID(),
                                             quoteBuyerBalance->getBalanceID(),
                                             baseAssetAmount, 5 * ONE, true, 0);
            quoteBuyerBalance = loadBalance(quoteBuyerBalance->getBalanceID(),
                                            app);
            REQUIRE(quoteBuyerBalance->getLocked() == quoteAssetAmount);
            REQUIRE(quoteBuyerBalance->getAmount() == 0);

            // delete
            offerTestHelper.applyManageOffer(buyer,
                               result.success().offer.offer().offerID,
                               baseBuyerBalance->getBalanceID(),
                               quoteBuyerBalance->getBalanceID(), 0, 5 * ONE,
                               true, 0);
            quoteBuyerBalance = loadBalance(quoteBuyerBalance->getBalanceID(),
                                            app);
            REQUIRE(quoteBuyerBalance->getLocked() == 0);
            REQUIRE(quoteBuyerBalance->getAmount() == quoteAssetAmount);
        }
        SECTION("base*price = quote")
        {
            auto buyerAccountID = buyer.key.getPublicKey();
            auto offerFeeFrame = FeeFrame::create(FeeType::OFFER_FEE, 0,
                                                  int64_t(0.2 * ONE), quote,
                                                  &buyerAccountID);
            auto offerFee = offerFeeFrame->getFee();

            applySetFees(app, root, rootSeq, &offerFee, false, nullptr);

            offerTestHelper.applyManageOffer(seller, 0,
                               baseSellerBalance->getBalanceID(),
                               quoteSellerBalance->getBalanceID(),
                               int64_t(0.5 * ONE), int64_t(45.11 * ONE), false, 0);
            offerTestHelper.applyManageOffer(seller, 0,
                               baseSellerBalance->getBalanceID(),
                               quoteSellerBalance->getBalanceID(),
                               int64_t(0.1 * ONE), int64_t(45.12 * ONE), false, 0);
            offerTestHelper.applyManageOffer(seller, 0,
                               baseSellerBalance->getBalanceID(),
                               quoteSellerBalance->getBalanceID(), ONE,
                               int64_t(45.13 * ONE), false, 0);

            int64_t fee = 0;
            OfferExchange::setFeeToPay(fee, int64_t(45.13 * ONE),
                                       int64_t(0.2 * ONE));
            auto result = offerTestHelper.applyManageOffer(buyer, 0,
                                             baseBuyerBalance->getBalanceID(),
                                             quoteBuyerBalance->getBalanceID(),
                                             ONE, int64_t(45.13 * ONE), true,
                                             fee);
            baseBuyerBalance = loadBalance(baseBuyerBalance->getBalanceID(),
                                           app);
            for (ClaimOfferAtom claimed : result.success().offersClaimed)
            {
                int64_t expectedQuote = 0;
                REQUIRE(bigDivide(expectedQuote, claimed.baseAmount, claimed.
                    currentPrice, ONE, ROUND_UP));
                REQUIRE(expectedQuote == claimed.quoteAmount);
            }
        }
        SECTION("3 taken by one")
        {
            auto buyerAccountID = buyer.key.getPublicKey();
            auto offerFeeFrame = FeeFrame::create(FeeType::OFFER_FEE, 0,
                                                  0.2 * ONE, quote,
                                                  &buyerAccountID);
            auto offerFee = offerFeeFrame->getFee();

            applySetFees(app, root, rootSeq, &offerFee, false, nullptr);

            offerTestHelper.applyManageOffer(seller, 0,
                               baseSellerBalance->getBalanceID(),
                               quoteSellerBalance->getBalanceID(),
                               int64_t(0.5 * ONE), int64_t(45.11 * ONE), false, 0);
            offerTestHelper.applyManageOffer(seller, 0,
                               baseSellerBalance->getBalanceID(),
                               quoteSellerBalance->getBalanceID(),
                               int64_t(0.1 * ONE), int64_t(45.12 * ONE), false, 0);
            offerTestHelper.applyManageOffer(seller, 0,
                               baseSellerBalance->getBalanceID(),
                               quoteSellerBalance->getBalanceID(), ONE,
                               int64_t(45.13 * ONE), false, 0);

            int64_t fee = 0;
            OfferExchange::setFeeToPay(fee, 45.13 * ONE, 0.2 * ONE);
            // round up to 2 digits
            fee = bigDivide(fee, 1, 100, ROUND_UP) * 100;
            auto result = offerTestHelper.applyManageOffer(buyer, 0,
                                             baseBuyerBalance->getBalanceID(),
                                             quoteBuyerBalance->getBalanceID(),
                                             ONE, int64_t(45.13 * ONE), true,
                                             fee);
            baseBuyerBalance = loadBalance(baseBuyerBalance->getBalanceID(),
                                           app);
            REQUIRE(baseBuyerBalance->getLocked() == 0);
            REQUIRE(baseBuyerBalance->getAmount() == ONE);
        }
        SECTION("1 to 0.5")
        {
            offerTestHelper.applyManageOffer(buyer, 0,
                               baseBuyerBalance->getBalanceID(),
                               quoteBuyerBalance->getBalanceID(),
                               baseAssetAmount, 5 * ONE, true, 0);
            quoteBuyerBalance = loadBalance(quoteBuyerBalance->getBalanceID(),
                                            app);
            REQUIRE(quoteBuyerBalance->getLocked() == quoteAssetAmount);
            REQUIRE(quoteBuyerBalance->getAmount() == 0);

            offerTestHelper.applyManageOffer(seller, 0,
                               baseSellerBalance->getBalanceID(),
                               quoteSellerBalance->getBalanceID(),
                               baseAssetAmount, 5 * ONE, false, 0);

            quoteBuyerBalance = loadBalance(quoteBuyerBalance->getBalanceID(),
                                            app);
            REQUIRE(quoteBuyerBalance->getLocked() == 0);
            REQUIRE(quoteBuyerBalance->getAmount() == 0);

            baseBuyerBalance = loadBalance(baseBuyerBalance->getBalanceID(),
                                           app);
            REQUIRE(baseBuyerBalance->getLocked() == 0);
            REQUIRE(baseBuyerBalance->getAmount() == baseAssetAmount);

            baseSellerBalance = loadBalance(baseSellerBalance->getBalanceID(),
                                            app);
            REQUIRE(baseSellerBalance->getLocked() == 0);
            REQUIRE(baseSellerBalance->getAmount() == 0);

            quoteSellerBalance = loadBalance(quoteSellerBalance->getBalanceID(),
                                             app);
            REQUIRE(quoteSellerBalance->getLocked() == 0);
            REQUIRE(quoteSellerBalance->getAmount() == quoteAssetAmount);
        }
        SECTION("0.5 to 1")
        {
            offerTestHelper.applyManageOffer(seller, 0,
                               baseSellerBalance->getBalanceID(),
                               quoteSellerBalance->getBalanceID(),
                               baseAssetAmount, 1 * ONE, false, 0);
            offerTestHelper.applyManageOffer(buyer, 0,
                               baseBuyerBalance->getBalanceID(),
                               quoteBuyerBalance->getBalanceID(),
                               baseAssetAmount - 1, 5 * ONE, true, 0);
            int64_t matchAmount = baseAssetAmount - 1;

            quoteBuyerBalance = loadBalance(quoteBuyerBalance->getBalanceID(),
                                            app);
            REQUIRE(quoteBuyerBalance->getLocked() == 0);
            int64_t matchQuoteAmount = matchAmount;
            REQUIRE(quoteBuyerBalance->getAmount() == (quoteAssetAmount -
                matchQuoteAmount));

            baseBuyerBalance = loadBalance(baseBuyerBalance->getBalanceID(),
                                           app);
            REQUIRE(baseBuyerBalance->getLocked() == 0);
            REQUIRE(baseBuyerBalance->getAmount() == matchAmount);

            baseSellerBalance = loadBalance(baseSellerBalance->getBalanceID(),
                                            app);
            REQUIRE(baseSellerBalance->getLocked() == 1);
            REQUIRE(baseSellerBalance->getAmount() == 0);

            quoteSellerBalance = loadBalance(quoteSellerBalance->getBalanceID(),
                                             app);
            REQUIRE(quoteSellerBalance->getLocked() == 0);
            REQUIRE(quoteSellerBalance->getAmount() == matchQuoteAmount);
        }
        SECTION("Asset pair is not tradable")
        {
            auto assetPairTestHelper = ManageAssetPairTestHelper(testManager);
            assetPairTestHelper.applyManageAssetPairTx(rootAccount, base, quote, 100 * ONE,
                                   0, 0,
                                   static_cast<int32_t>(AssetPairPolicy::
                                       TRADEABLE_SECONDARY_MARKET),
                                   ManageAssetPairAction::UPDATE_POLICIES);
            auto offerResult = offerTestHelper.applyManageOffer(seller, 0,
                                                  baseSellerBalance->
                                                  getBalanceID(),
                                                  quoteSellerBalance->
                                                  getBalanceID(),
                                                  baseAssetAmount, 95 * ONE,
                                                  false, 0);
            assetPairTestHelper.applyManageAssetPairTx(rootAccount, base, quote, 101 * ONE,
                                   0, 0, 0,
                                   ManageAssetPairAction::UPDATE_POLICIES);
            // offer was deleted
            auto offer = OfferHelper::Instance()->loadOffer(seller.key.getPublicKey(),
                              offerResult.success().offer.offer().offerID, testManager->getDB());
            REQUIRE(!offer);
            // can place offer
            offerTestHelper.applyManageOffer(seller, 0,
                               baseSellerBalance->getBalanceID(),
                               quoteSellerBalance->getBalanceID(),
                               baseAssetAmount, 95 * ONE, false, 0,
                               ManageOfferResultCode::ASSET_PAIR_NOT_TRADABLE);
        }
        SECTION(
            "Seller can receive more then we expected based on base amount and price"
        )
        {
            int64_t sellPrice = int64_t(45.76 * ONE);
            int64_t buyPrice = int64_t(45.77 * ONE);
            offerTestHelper.applyManageOffer(buyer, 0,
                               baseBuyerBalance->getBalanceID(),
                               quoteBuyerBalance->getBalanceID(), ONE, buyPrice,
                               true, 0);
            auto offerMatch = offerTestHelper.applyManageOffer(seller, 0,
                                                 baseSellerBalance->
                                                 getBalanceID(),
                                                 quoteSellerBalance->
                                                 getBalanceID(), ONE, sellPrice,
                                                 false, 0);
            int64_t baseAmount = ONE;
            int64_t quoteAmount = buyPrice;
            REQUIRE(offerMatch.success().offersClaimed[0].baseAmount ==
                baseAmount);
            REQUIRE(offerMatch.success().offersClaimed[0].quoteAmount ==
                quoteAmount);
        }
        SECTION("buy amount == sell amount - buy price")
        {
            int64_t buyAmount = baseAssetAmount;
            int64_t sellAmount = buyAmount;
            int64_t buyPrice = 3 * ONE;
            int64_t sellPrice = ONE;
            offerTestHelper.applyManageOffer(buyer, 0,
                               baseBuyerBalance->getBalanceID(),
                               quoteBuyerBalance->getBalanceID(), buyAmount,
                               buyPrice, true, 0);
            auto offerMatch = offerTestHelper.applyManageOffer(seller, 0,
                                                 baseSellerBalance->
                                                 getBalanceID(),
                                                 quoteSellerBalance->
                                                 getBalanceID(), sellAmount,
                                                 sellPrice, false, 0);
            REQUIRE(offerMatch.success().offersClaimed[0].currentPrice ==
                buyPrice);
        }
        SECTION("buy amount > sell amount - buy price")
        {
            int64_t buyAmount = baseAssetAmount;
            int64_t sellAmount = buyAmount - 1;
            int64_t buyPrice = 3 * ONE;
            int64_t sellPrice = ONE;
            offerTestHelper.applyManageOffer(buyer, 0,
                               baseBuyerBalance->getBalanceID(),
                               quoteBuyerBalance->getBalanceID(), buyAmount,
                               buyPrice, true, 0);
            auto offerMatch = offerTestHelper.applyManageOffer(seller, 0,
                                                 baseSellerBalance->
                                                 getBalanceID(),
                                                 quoteSellerBalance->
                                                 getBalanceID(), sellAmount,
                                                 sellPrice, false, 0);
            REQUIRE(offerMatch.success().offersClaimed[0].currentPrice ==
                buyPrice);
        }
        SECTION("buy amount < sell amount - sell price")
        {
            int64_t sellAmount = baseAssetAmount;
            int64_t buyAmount = sellAmount - 1;
            int64_t buyPrice = 3 * ONE;
            int64_t sellPrice = ONE;
            offerTestHelper.applyManageOffer(seller, 0,
                               baseSellerBalance->getBalanceID(),
                               quoteSellerBalance->getBalanceID(), sellAmount,
                               sellPrice, false, 0);
            auto offerMatch = offerTestHelper.applyManageOffer(buyer, 0,
                                                 baseBuyerBalance->
                                                 getBalanceID(),
                                                 quoteBuyerBalance->
                                                 getBalanceID(), buyAmount,
                                                 buyPrice, true, 0);
            REQUIRE(offerMatch.success().offersClaimed[0].currentPrice ==
                sellPrice);
        }
        SECTION("Offer fees")
        {
            auto offerFeeFrame = FeeFrame::create(FeeType::OFFER_FEE, 0, ONE,
                                                  quote);
            auto offerFee = offerFeeFrame->getFee();

            applySetFees(app, root, rootSeq, &offerFee, false, nullptr);
            SECTION("Try to spend all quote")
            {
                int64_t offerPrice = 5 * ONE;
                int64_t feeToPay = 0;
                REQUIRE(OfferExchange::setFeeToPay(feeToPay, quoteAssetAmount,
                    ONE));
                offerTestHelper.applyManageOffer(buyer, 0,
                                   baseBuyerBalance->getBalanceID(),
                                   quoteBuyerBalance->getBalanceID(),
                                   quoteAssetAmount / offerPrice * ONE,
                                   offerPrice, true, feeToPay,
                                   ManageOfferResultCode::UNDERFUNDED);
            }
            SECTION("Not 0 percent fee")
            {
                offerTestHelper.applyManageOffer(seller, 0,
                                   baseSellerBalance->getBalanceID(),
                                   quoteSellerBalance->getBalanceID(),
                                   baseAssetAmount, 1 * ONE, false, 0,
                                   ManageOfferResultCode::MALFORMED);
            }
            SECTION("Success")
            {
                auto buyerAccountID = baseBuyerBalance->getAccountID();
                offerFeeFrame = FeeFrame::create(FeeType::OFFER_FEE, 0, 2 * ONE,
                                                 quote, &buyerAccountID);
                offerFee = offerFeeFrame->getFee();
                applySetFees(app, root, rootSeq, &offerFee, false, nullptr);
                int64_t matchAmount = baseAssetAmount;
                int64_t matchPrice = 4 * ONE;
                int64_t quoteAssetMatchAmount = matchAmount * matchPrice / ONE;

                int64_t sellerMatchFee, buyerOfferFeeToLock, sellerOfferFee;
                REQUIRE(OfferExchange::setFeeToPay(sellerMatchFee,
                    quoteAssetMatchAmount, ONE));
                REQUIRE(OfferExchange::setFeeToPay(sellerOfferFee,
                    baseAssetAmount, ONE));
                REQUIRE(OfferExchange::setFeeToPay(buyerOfferFeeToLock,
                    baseAssetAmount * 4, offerFee.percentFee));
                offerTestHelper.applyManageOffer(seller, 0,
                                   baseSellerBalance->getBalanceID(),
                                   quoteSellerBalance->getBalanceID(),
                                   baseAssetAmount, ONE, false, sellerOfferFee);
                auto offerMatch = offerTestHelper.applyManageOffer(buyer, 0,
                                                     baseBuyerBalance->
                                                     getBalanceID(),
                                                     quoteBuyerBalance->
                                                     getBalanceID(),
                                                     baseAssetAmount,
                                                     matchPrice, true,
                                                     buyerOfferFeeToLock);
                REQUIRE(offerMatch.success().offersClaimed[0].quoteAmount ==
                    quoteAssetMatchAmount);
                REQUIRE(offerMatch.success().offersClaimed[0].bFeePaid ==
                    sellerMatchFee);
                int64_t buyerOfferFee = 0;
                REQUIRE(OfferExchange::setFeeToPay(buyerOfferFee,
                    quoteAssetMatchAmount, offerFee.percentFee));
                REQUIRE(offerMatch.success().offersClaimed[0].aFeePaid ==
                    buyerOfferFee);
                REQUIRE(offerMatch.success().offersClaimed.size() == 1);

                quoteBuyerBalance =
                    loadBalance(quoteBuyerBalance->getBalanceID(), app);
                REQUIRE(quoteBuyerBalance->getLocked() == 0);
                REQUIRE(quoteBuyerBalance->getAmount() == (quoteAssetAmount -
                    quoteAssetMatchAmount - buyerOfferFee));

                baseBuyerBalance = loadBalance(baseBuyerBalance->getBalanceID(),
                                               app);
                REQUIRE(baseBuyerBalance->getLocked() == 0);
                REQUIRE(baseBuyerBalance->getAmount() == baseAssetAmount);

                baseSellerBalance =
                    loadBalance(baseSellerBalance->getBalanceID(), app);
                REQUIRE(baseSellerBalance->getLocked() == 0);
                REQUIRE(baseSellerBalance->getAmount() == 0);

                quoteSellerBalance =
                    loadBalance(quoteSellerBalance->getBalanceID(), app);
                REQUIRE(quoteSellerBalance->getLocked() == 0);
                REQUIRE(quoteSellerBalance->getAmount() == quoteAssetMatchAmount
                    - sellerMatchFee);

                auto commissionQuoteBalance = balanceHelper->
                    loadBalance(app.getCommissionID(), quote, db, &delta);
                REQUIRE(commissionQuoteBalance);
                REQUIRE(commissionQuoteBalance->getAmount() == buyerOfferFee +
                    sellerMatchFee);
            }
        }
    }
    SECTION("Random tests")
    {
        int64_t fee = int64_t(0.75 * ONE);
        auto offerFeeFrame = FeeFrame::
            create(FeeType::OFFER_FEE, 0, fee, quote);
        auto offerFee = offerFeeFrame->getFee();
        applySetFees(app, root, rootSeq, &offerFee, false, nullptr);

        auto buyer = Account{ SecretKey::random(), 0 };
        applyCreateAccountTx(app, root, buyer.key, rootSeq, AccountType::GENERAL);
        auto baseBuyerBalance = balanceHelper->loadBalance(buyer.key.getPublicKey(),
                                                           base, db, &delta);
        REQUIRE(baseBuyerBalance);
        auto quoteBuyerBalance = balanceHelper->
            loadBalance(buyer.key.getPublicKey(), quote, db, &delta);
        REQUIRE(quoteBuyerBalance);
        auto quoteAssetAmount = 10000 * ONE;
        fundAccount(quote, quoteAssetAmount, quoteBuyerBalance->getBalanceID());

        auto seller = Account{ SecretKey::random() , 0};
        applyCreateAccountTx(app, root, seller.key, rootSeq, AccountType::GENERAL);
        auto baseSellerBalance = balanceHelper->
            loadBalance(seller.key.getPublicKey(), base, db, &delta);
        REQUIRE(baseSellerBalance);
        auto quoteSellerBalance = balanceHelper->
            loadBalance(seller.key.getPublicKey(), quote, db, &delta);
        REQUIRE(quoteSellerBalance);
        auto baseAssetAmount = 1000 * ONE;
        fundAccount(base, baseAssetAmount, baseSellerBalance->getBalanceID());

        auto randomPrice = [&base, &quote, &testManager](bool isBuy)
        {
            std::vector<OfferFrame::pointer> offers;
            const int maxOrders = 10;
            OfferHelper::Instance()->loadBestOffers(maxOrders, 0, base, quote, 0, !isBuy, offers, testManager->getDB());
            if (offers.size() == maxOrders)
            {
                return offers[offers.size() - 1]->getPrice();
            }
            // to ensure that quote amount is >= 1
            return (rand() % INT16_MAX + ONE);
        };

        auto randomAmount = []()
        {
            // to ensure that base amount > 1;
            return (rand() % INT16_MAX + 1);
        };

        int64_t matchesLeft = 1000;

        // buyer and seller are placing ramdom orders untill one of them runs out of money
        while (matchesLeft > 0)
        {
            int64_t buyerAmount = randomAmount();
            int64_t buyerPrice = randomPrice(true);
            quoteBuyerBalance = balanceHelper->loadBalance(buyer.key.getPublicKey(),
                                                           quote, db, &delta);
            int64_t buyerOfferAmount = buyerAmount * buyerPrice;
            assert(buyerOfferAmount > 0);

            int64_t buyerFee = 0;
            REQUIRE(OfferExchange::setFeeToPay(buyerFee, bigDivide(buyerPrice,
                buyerAmount, int64_t(ONE), ROUND_UP), fee));

            if (quoteBuyerBalance->getAmount() < buyerOfferAmount + buyerFee)
            {
                quoteAssetAmount += buyerOfferAmount + buyerFee;
                assert(quoteAssetAmount > 0);
                fundAccount(quote, buyerOfferAmount + buyerFee, quoteBuyerBalance->getBalanceID());
            }

            auto offerResult = offerTestHelper.applyManageOffer(buyer, 0, baseBuyerBalance->
                getBalanceID(),
                quoteBuyerBalance->
                getBalanceID(), buyerAmount,
                buyerPrice, true, buyerFee);
            matchesLeft -= offerResult.success().offersClaimed.size();

            int64_t sellerAmount = randomAmount();
            baseSellerBalance = balanceHelper->
                loadBalance(seller.key.getPublicKey(), base, db, &delta);
            if (baseSellerBalance->getAmount() < sellerAmount)
            {
                baseAssetAmount += sellerAmount;
                assert(baseAssetAmount > 0);
                fundAccount(base, sellerAmount, baseSellerBalance->getBalanceID());
            }

            // to ensure that quote amount is >= 1
            int64_t sellerPrice = randomPrice(false);
            int64_t sellerFee = 0;
            REQUIRE(OfferExchange::setFeeToPay(sellerFee, bigDivide(sellerAmount
                , sellerPrice, int64_t(ONE), ROUND_UP), fee));
            offerResult = offerTestHelper.applyManageOffer(seller, 0,
                baseSellerBalance->getBalanceID(),
                quoteSellerBalance->getBalanceID(),
                sellerAmount, sellerPrice, false,
                sellerFee);
            matchesLeft -= offerResult.success().offersClaimed.size();
            LOG(INFO) << "matches left: " << matchesLeft;
        }

        auto offersByAccount = offerHelper->loadAllOffers(app.getDatabase());
        // delete all buyers offers
        auto buyersOffers = offersByAccount[buyer.key.getPublicKey()];
        for (auto buyerOffer : buyersOffers)
        {
            offerTestHelper.applyManageOffer(buyer, buyerOffer->getOfferID(),
                               baseBuyerBalance->getBalanceID(),
                               quoteBuyerBalance->getBalanceID(), 0, 1, true, 0);
        }

        // delete all seller offers
        auto sellerOffers = offersByAccount[seller.key.getPublicKey()];
        for (auto sellerOffer : sellerOffers)
        {
            offerTestHelper.applyManageOffer(seller, sellerOffer->getOfferID(),
                               baseSellerBalance->getBalanceID(),
                               quoteSellerBalance->getBalanceID(), 0, 1, false, 0);
        }

        offersByAccount = offerHelper->loadAllOffers(app.getDatabase());
        REQUIRE(offersByAccount.size() == 0);

        baseBuyerBalance = balanceHelper->loadBalance(buyer.key.getPublicKey(),
                                                      base, db, &delta);
        // there must be matches
        REQUIRE(baseBuyerBalance->getAmount() != 0);
        REQUIRE(baseBuyerBalance->getLocked() == 0);

        quoteBuyerBalance = balanceHelper->loadBalance(buyer.key.getPublicKey(),
                                                       quote, db, &delta);
        REQUIRE(quoteBuyerBalance->getLocked() == 0);

        baseSellerBalance = balanceHelper->loadBalance(seller.key.getPublicKey(),
                                                       base, db, &delta);
        REQUIRE(baseSellerBalance->getLocked() == 0);

        quoteSellerBalance = balanceHelper->loadBalance(seller.key.getPublicKey(),
                                                        quote, db, &delta);
        // there must be matches
        REQUIRE(quoteSellerBalance->getAmount() != 0);
        REQUIRE(quoteSellerBalance->getLocked() == 0);

        REQUIRE(baseBuyerBalance->getAmount() + baseSellerBalance->getAmount()
            == baseAssetAmount);

        auto comissionAccount = balanceHelper->
            loadBalance(app.getCommissionID(), quote, db, nullptr);
        REQUIRE(comissionAccount);
        REQUIRE(comissionAccount->getAmount() + quoteBuyerBalance->getAmount() +
            quoteSellerBalance->getAmount() == quoteAssetAmount);
    }
}
