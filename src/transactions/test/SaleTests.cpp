// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0
#include <ledger/OfferHelper.h>
#include <ledger/AssetPairHelper.h>
#include "main/Application.h"
#include "main/test.h"
#include "TxTests.h"
#include "crypto/SHA.h"
#include "test_helper/TestManager.h"
#include "test_helper/Account.h"
#include "test_helper/ManageAssetTestHelper.h"
#include "ledger/BalanceHelper.h"
#include "test_helper/CreateAccountTestHelper.h"
#include "test_helper/SaleRequestHelper.h"
#include "test_helper/IssuanceRequestHelper.h"
#include "test_helper/ManageBalanceTestHelper.h"
#include "test_helper/ParticipateInSaleTestHelper.h"
#include "transactions/dex/OfferManager.h"
#include "ledger/SaleHelper.h"
#include "test_helper/CheckSaleStateTestHelper.h"
#include "test/test_marshaler.h"

using namespace stellar;
using namespace stellar::txtest;

typedef std::unique_ptr<Application> appPtr;

void addNewParticipant(TestManager::pointer testManager, Account& root, const uint64_t saleID, const AssetCode baseAsset,
                       const AssetCode quoteAsset, const uint64_t quoteAssetAmount, const uint64_t price, const uint64_t fee)
{
    auto account = Account{ SecretKey::random(), 0 };
    CreateAccountTestHelper(testManager).applyCreateAccountTx(root, account.key.getPublicKey(), AccountType::NOT_VERIFIED);
    auto quoteBalance = BalanceHelper::Instance()->loadBalance(account.key.getPublicKey(), quoteAsset, testManager->getDB(), nullptr);
    REQUIRE(!!quoteBalance);
    IssuanceRequestHelper(testManager).applyCreateIssuanceRequest(root, quoteAsset, quoteAssetAmount, quoteBalance->getBalanceID(),
        SecretKey::random().getStrKeyPublic());
    auto accountID = account.key.getPublicKey();
    auto balanceCreationResult = ManageBalanceTestHelper(testManager).applyManageBalanceTx(account, accountID, baseAsset);
    const auto baseAssetAmount = bigDivide(quoteAssetAmount, ONE, price, ROUND_UP);
    auto manageOfferOp = OfferManager::buildManageOfferOp(balanceCreationResult.success().balanceID, quoteBalance->getBalanceID(),
        true, baseAssetAmount, price, fee, 0, saleID);
    ParticipateInSaleTestHelper(testManager).applyManageOffer(account, manageOfferOp);
}

TEST_CASE("Sale", "[tx][sale]")
{

    Config const& cfg = getTestConfig(0, Config::TESTDB_POSTGRESQL);
    VirtualClock clock;
    Application::pointer appPtr = Application::create(clock, cfg);
    Application& app = *appPtr;
    app.start();
    auto testManager = TestManager::make(app);

    auto root = Account{ getRoot(), Salt(0) };

    AssetCode quoteAsset = "USD";
    auto assetTestHelper = ManageAssetTestHelper(testManager);
    uint64_t quoteMaxIssuance = INT64_MAX;
    auto assetCreationRequest = assetTestHelper.createAssetCreationRequest(quoteAsset, root.key.getPublicKey(), "{}", quoteMaxIssuance,
                                                                           uint32_t(AssetPolicy::BASE_ASSET));
    assetTestHelper.applyManageAssetTx(root, 0, assetCreationRequest);

    // test helpers
    CreateAccountTestHelper createAccountTestHelper(testManager);
    SaleRequestHelper saleRequestHelper(testManager);
    IssuanceRequestHelper issuanceHelper(testManager);
    CheckSaleStateHelper checkStateHelper(testManager);

    SECTION("Happy path")
    {
        auto syndicate = Account{ SecretKey::random(), 0 };
        auto syndicatePubKey = syndicate.key.getPublicKey();
        auto offerFeeFrame = FeeFrame::create(FeeType::OFFER_FEE, 0,
            int64_t(0.2 * ONE), quoteAsset,
            &syndicatePubKey);
        auto offerFee = offerFeeFrame->getFee();

        CreateAccountTestHelper(testManager).applyCreateAccountTx(root, syndicatePubKey, AccountType::SYNDICATE);
        const AssetCode baseAsset = "BTC";
        const auto maxIssuanceAmount = 1000 * ONE;
        assetCreationRequest = assetTestHelper.createAssetCreationRequest(baseAsset, syndicate.key.getPublicKey(), "{}", maxIssuanceAmount,0, maxIssuanceAmount);
        assetTestHelper.createApproveRequest(root, syndicate, assetCreationRequest);
        const auto currentTime = testManager->getLedgerManager().getCloseTime();
        const auto endTime = currentTime + 1000;
        const auto price = 2 * ONE;
        const auto hardCap = bigDivide(maxIssuanceAmount, price, ONE, ROUND_DOWN);
        const auto softCap = hardCap / 2;
        const auto saleRequest = saleRequestHelper.createSaleRequest(baseAsset, quoteAsset, currentTime, endTime, price, softCap, hardCap, "{}");
        saleRequestHelper.createApprovedSale(root, syndicate, saleRequest);
        auto accountTestHelper = CreateAccountTestHelper(testManager);
        IssuanceRequestHelper(testManager).authorizePreIssuedAmount(root, root.key, quoteAsset, hardCap, root);
        auto sales = SaleHelper::Instance()->loadSales(baseAsset, quoteAsset, testManager->getDB());
        REQUIRE(sales.size() == 1);
        const auto saleID = sales[0]->getID();
        SECTION("Reached hard cap")
        {
            const int numberOfParticipants = 10;
            for (auto i = 0; i < numberOfParticipants; i++)
            {
                
                const auto quoteAssetAmount = hardCap / numberOfParticipants;
                addNewParticipant(testManager, root, saleID, baseAsset, quoteAsset, quoteAssetAmount, price, 0);
                if (i < numberOfParticipants - 1)
                {
                    CheckSaleStateHelper(testManager).applyCheckSaleStateTx(root, CheckSaleStateResultCode::NO_SALES_FOUND);
                }
            }

            CheckSaleStateHelper(testManager).applyCheckSaleStateTx(root);
        }
        SECTION("Canceled")
        {
            const int numberOfParticipants = 10;
            for (auto i = 0; i < numberOfParticipants - 1; i++)
            {

                addNewParticipant(testManager, root, saleID, baseAsset, quoteAsset, softCap / numberOfParticipants, price, 0);
                checkStateHelper.applyCheckSaleStateTx(root, CheckSaleStateResultCode::NO_SALES_FOUND);
            }
            // softcap is not reached, so no sale to close
            CheckSaleStateHelper(testManager).applyCheckSaleStateTx(root, CheckSaleStateResultCode::NO_SALES_FOUND);
            // close ledger after end time
            testManager->advanceToTime(endTime + 1);
            auto checkRes = checkStateHelper.applyCheckSaleStateTx(root, CheckSaleStateResultCode::SUCCESS);
            REQUIRE(checkRes.success().effect.effect() == CheckSaleStateEffect::CANCELED);
        }
    }

    SECTION("Participation")
    {
        Database& db = testManager->getDB();
        ParticipateInSaleTestHelper participateHelper(testManager);

        // create sale owner
        Account owner = Account{ SecretKey::random(), Salt(0) };
        createAccountTestHelper.applyCreateAccountTx(root, owner.key.getPublicKey(), AccountType::SYNDICATE);

        // create base asset
        const AssetCode baseAsset = "ETH";
        uint64_t maxIssuanceAmount = 10 * ONE;
        auto baseAssetRequest = assetTestHelper.createAssetCreationRequest(baseAsset, owner.key.getPublicKey(), "{}",
                                                                               maxIssuanceAmount, 0, maxIssuanceAmount);
        assetTestHelper.createApproveRequest(root, owner, baseAssetRequest);

        // create participant
        Account participant = Account{ SecretKey::random(), Salt(0) };
        AccountID participantID = participant.key.getPublicKey();
        createAccountTestHelper.applyCreateAccountTx(root, participantID, AccountType::GENERAL);

        // create base balance for participant:
        auto manageBalanceRes = ManageBalanceTestHelper(testManager).applyManageBalanceTx(participant, participantID, baseAsset);
        BalanceID baseBalance = manageBalanceRes.success().balanceID;
        BalanceID quoteBalance = BalanceHelper::Instance()->loadBalance(participantID, quoteAsset, db,
                                                                        nullptr)->getBalanceID();

        // pre-issue quote amount
        uint64_t quotePreIssued = quoteMaxIssuance - 1;
        issuanceHelper.authorizePreIssuedAmount(root, root.key, quoteAsset, quotePreIssued, root);

        SECTION("malformed manage offer")
        {
            LedgerDelta delta(testManager->getLedgerManager().getCurrentLedgerHeader(), db);
            // create sale to participate in:
            uint64_t startTime = testManager->getLedgerManager().getCloseTime() + 100;
            uint64_t endTime = startTime + 1000;
            uint64_t price = 2 * ONE;
            int64_t hardCap = bigDivide(maxIssuanceAmount, price, ONE, ROUND_UP);
            auto saleRequest = saleRequestHelper.createSaleRequest(baseAsset, quoteAsset, startTime, endTime,
                                                                   price, hardCap/2, hardCap, "{}");
            saleRequestHelper.createApprovedSale(root, owner, saleRequest);
            auto sales = SaleHelper::Instance()->loadSales(baseAsset, quoteAsset, db);
            uint64_t saleID = sales[0]->getID();

            // fund participant with quote asset
            uint64_t quoteBalanceAmount = saleRequest.hardCap;
            issuanceHelper.applyCreateIssuanceRequest(root, quoteAsset, quoteBalanceAmount, quoteBalance,
                                                      SecretKey::random().getStrKeyPublic());

            // buy a half of sale in order to keep it active
            int64_t baseAmount = bigDivide(saleRequest.hardCap/2, ONE, saleRequest.price, ROUND_UP);
            auto manageOffer = OfferManager::buildManageOfferOp(baseBalance, quoteBalance, true, baseAmount,
                                                                saleRequest.price, 0, 0, saleID);

            SECTION("try to participate in not started sale")
            {
                participateHelper.applyManageOffer(participant, manageOffer, ManageOfferResultCode::SALE_IS_NOT_STARTED_YET);
            }

            // close ledger on start time
            testManager->advanceToTime(startTime);

            SECTION("successfully create participation then delete it")
            {
                participateHelper.applyManageOffer(participant, manageOffer);

                auto offers = OfferHelper::Instance()->loadOffersWithFilters(baseAsset, quoteAsset, &saleID, nullptr, db);
                REQUIRE(offers.size() == 1);

                manageOffer.amount = 0;
                manageOffer.offerID = offers[0]->getOfferID();
                participateHelper.applyManageOffer(participant, manageOffer);
            }
            SECTION("try to sell base asset being participant")
            {
                manageOffer.isBuy = false;
                participateHelper.applyManageOffer(participant, manageOffer, ManageOfferResultCode::MALFORMED);
            }
            SECTION("try to participate with negative amount")
            {
                manageOffer.amount = -1;
                participateHelper.applyManageOffer(participant, manageOffer, ManageOfferResultCode::INVALID_AMOUNT);
            }
            SECTION("try to participate with zero price")
            {
                manageOffer.price = 0;
                participateHelper.applyManageOffer(participant, manageOffer, ManageOfferResultCode::PRICE_IS_INVALID);
            }
            SECTION("overflow quote amount")
            {
                manageOffer.amount = bigDivide(INT64_MAX, ONE, manageOffer.price, ROUND_UP) + 1;
                participateHelper.applyManageOffer(participant, manageOffer, ManageOfferResultCode::INVALID_AMOUNT);
            }
            SECTION("negative fee")
            {
                manageOffer.fee = -1;
                participateHelper.applyManageOffer(participant, manageOffer, ManageOfferResultCode::INVALID_PERCENT_FEE);
            }
            SECTION("base balance == quote balance")
            {
                manageOffer.baseBalance = manageOffer.quoteBalance;
                participateHelper.applyManageOffer(participant, manageOffer, ManageOfferResultCode::ASSET_PAIR_NOT_TRADABLE);
            }
            SECTION("base balance doesn't exist")
            {
                BalanceID nonExistingBalance = SecretKey::random().getPublicKey();
                manageOffer.baseBalance = nonExistingBalance;
                participateHelper.applyManageOffer(participant, manageOffer, ManageOfferResultCode::BALANCE_NOT_FOUND);
            }
            SECTION("quote balance doesn't exist")
            {
                BalanceID nonExistingBalance = SecretKey::random().getPublicKey();
                manageOffer.quoteBalance = nonExistingBalance;
                participateHelper.applyManageOffer(participant, manageOffer, ManageOfferResultCode::BALANCE_NOT_FOUND);
            }
            SECTION("asset pair for base and quote doesn't exist")
            {
                // delete asset pair
                auto assetPair = AssetPairHelper::Instance()->loadAssetPair(baseAsset, quoteAsset, db);
                REQUIRE(assetPair);
                EntryHelperProvider::storeDeleteEntry(delta, db, assetPair->getKey());

                participateHelper.applyManageOffer(participant, manageOffer, ManageOfferResultCode::ASSET_PAIR_NOT_TRADABLE);
            }
            SECTION("try participate in non-existing sale")
            {
                uint64_t nonExistingSaleID = saleID + 1;
                manageOffer.orderBookID = nonExistingSaleID;
                participateHelper.applyManageOffer(participant, manageOffer, ManageOfferResultCode::ORDER_BOOK_DOES_NOT_EXISTS);
            }
            SECTION("base and quote balances are in the same asset")
            {
                //create one more balance in base asset:
                auto opRes = ManageBalanceTestHelper(testManager).applyManageBalanceTx(participant, participantID, baseAsset);
                auto baseBalanceID = opRes.success().balanceID;
                manageOffer.quoteBalance = baseBalanceID;

                // can't find a sale from base to base
                participateHelper.applyManageOffer(participant, manageOffer, ManageOfferResultCode::ORDER_BOOK_DOES_NOT_EXISTS);
            }
            SECTION("price doesn't match sales price")
            {
                manageOffer.price = saleRequest.price + 1;
                participateHelper.applyManageOffer(participant, manageOffer, ManageOfferResultCode::PRICE_DOES_NOT_MATCH);
            }
            SECTION("try to participate in own sale")
            {
                // load balances for owner
                auto quoteBalanceID = BalanceHelper::Instance()->loadBalance(owner.key.getPublicKey(), quoteAsset, db,
                                                                                nullptr)->getBalanceID();
                auto baseBalanceID = AccountManager::loadOrCreateBalanceForAsset(owner.key.getPublicKey(), baseAsset, db, delta);

                manageOffer.baseBalance = baseBalanceID;
                manageOffer.quoteBalance = quoteBalanceID;
                participateHelper.applyManageOffer(owner, manageOffer, ManageOfferResultCode::CANT_PARTICIPATE_OWN_SALE);
            }
            SECTION("amount exceeds hard cap")
            {
                // fund account
                issuanceHelper.applyCreateIssuanceRequest(root, quoteAsset, 2 * ONE, quoteBalance,
                                                          SecretKey::random().getStrKeyPublic());
                SECTION("by less than ONE")
                {
                    int64_t baseAssetAmount = bigDivide(hardCap + ONE/2, ONE, price, ROUND_DOWN);
                    manageOffer.amount = baseAssetAmount;
                    participateHelper.applyManageOffer(participant, manageOffer);

                    checkStateHelper.applyCheckSaleStateTx(root, CheckSaleStateResultCode::SUCCESS);
                }
                SECTION("by more than ONE")
                {
                    int64_t baseAssetAmount = bigDivide(hardCap + 2 * ONE, ONE, price, ROUND_DOWN);
                    manageOffer.amount = baseAssetAmount;
                    participateHelper.applyManageOffer(participant, manageOffer,
                                                       ManageOfferResultCode::ORDER_VIOLATES_HARD_CAP);
                }
            }
            SECTION("underfunded")
            {
                // participent has ONE/2 less quote amount than he want to exchange
                int64_t baseAssetAmount = bigDivide(quoteBalanceAmount + ONE/2, ONE, price, ROUND_DOWN);
                manageOffer.amount = baseAssetAmount;
                participateHelper.applyManageOffer(participant, manageOffer, ManageOfferResultCode::UNDERFUNDED);
            }
            SECTION("delete participation")
            {
                // create sale participation:
                int64_t initialAmount = manageOffer.amount;
                participateHelper.applyManageOffer(participant, manageOffer);
                auto offers = OfferHelper::Instance()->loadOffersWithFilters(baseAsset, quoteAsset, &saleID, nullptr, db);
                REQUIRE(offers.size() == 1);
                uint64_t offerID = offers[0]->getOfferID();

                //delete offer
                manageOffer.amount = 0;
                manageOffer.offerID = offerID;

                SECTION("try to delete non-existing offer")
                {
                    //switch to non-existing offerID
                    manageOffer.offerID++;
                    participateHelper.applyManageOffer(participant, manageOffer, ManageOfferResultCode::NOT_FOUND);
                }
                SECTION("try to delete from non-existing orderBook")
                {
                    uint64_t nonExistingOrderBookID = saleID + 1;
                    manageOffer.orderBookID = nonExistingOrderBookID;
                    participateHelper.applyManageOffer(participant, manageOffer, ManageOfferResultCode::NOT_FOUND);
                }
                SECTION("try to delete closed sale")
                {
                    //participate again in order to close sale
                    int64_t baseHardCap = bigDivide(hardCap, ONE, price, ROUND_DOWN);
                    manageOffer.amount = baseHardCap - initialAmount;
                    manageOffer.offerID = 0;
                    participateHelper.applyManageOffer(participant, manageOffer);

                    //try to delete offer
                    manageOffer.offerID = offerID;
                    manageOffer.amount = 0;
                    participateHelper.applyManageOffer(participant, manageOffer, ManageOfferResultCode::SALE_IS_NOT_ACTIVE);
                }
            }
            SECTION("try to participate after end time")
            {
                testManager->advanceToTime(endTime + 1);
                participateHelper.applyManageOffer(participant, manageOffer, ManageOfferResultCode::SALE_ALREADY_ENDED);
            }
        }
    }
}
