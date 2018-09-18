#include <ledger/AssetPairHelper.h>
#include <transactions/FeesManager.h>
#include "main/Application.h"
#include "main/test.h"
#include "TxTests.h"
#include "src/util/types.h"
#include "test_helper/TestManager.h"
#include "test_helper/Account.h"
#include "test_helper/ManageAssetTestHelper.h"
#include "test_helper/CreateAccountTestHelper.h"
#include "test_helper/SaleRequestHelper.h"
#include "test_helper/IssuanceRequestHelper.h"
#include "test_helper/CheckSaleStateTestHelper.h"
#include "test/test_marshaler.h"
#include "ledger/SaleHelper.h"
#include "test_helper/ManageAssetPairTestHelper.h"
#include "ledger/BalanceHelperLegacy.h"
#include "test_helper/ManageBalanceTestHelper.h"
#include "transactions/dex/OfferManager.h"
#include "test_helper/ParticipateInSaleTestHelper.h"
#include "test_helper/ManageSaleTestHelper.h"
#include "test_helper/ReviewPromotionUpdateRequestTestHelper.h"

using namespace stellar;
using namespace stellar::txtest;

typedef std::unique_ptr<Application> appPtr;

TEST_CASE("Crowdfunding vs fixed price", "[tx][fixedprice][crowdfund]"){

    Config const& cfg = getTestConfig(0, Config::TESTDB_POSTGRESQL);
    VirtualClock clock;
    const Application::pointer appPtr = Application::create(clock, cfg);
    Application& app = *appPtr;
    app.start();
    auto testManager = TestManager::make(app);
    TestManager::upgradeToCurrentLedgerVersion(app);

    auto root = Account{ getRoot(), Salt(0) };

    const AssetCode defaultQuoteAsset = "USD";
    auto assetTestHelper = ManageAssetTestHelper(testManager);
    auto assetCreationRequest = assetTestHelper.createAssetCreationRequest(defaultQuoteAsset, root.key.getPublicKey(), "{}", INT64_MAX,
                                                                           uint32_t(AssetPolicy::BASE_ASSET), INT64_MAX);
    assetTestHelper.applyManageAssetTx(root, 0, assetCreationRequest);

    CreateAccountTestHelper createAccountTestHelper(testManager);
    SaleRequestHelper saleRequestHelper(testManager);
    IssuanceRequestHelper issuanceHelper(testManager);
    CheckSaleStateHelper checkSaleStateHelper(testManager);
    ManageSaleTestHelper manageSaleHelper(testManager);
    ReviewPromotionUpdateRequestHelper reviewPromotionUpdateHelper(testManager);
    ManageBalanceTestHelper balanceTestHelper(testManager);
    ManageOfferTestHelper offerTestHelper(testManager);

    Database& db = app.getDatabase();

    auto syndicate = Account{ SecretKey::random(), 0 };
    const auto syndicatePubKey = syndicate.key.getPublicKey();

    createAccountTestHelper.applyCreateAccountTx(root, syndicatePubKey, AccountType::SYNDICATE);
    const AssetCode baseAsset = "BTC";
    const uint64_t maxIssuanceAmount = 1000 * ONE;
    const uint64_t preIssuedAmount = maxIssuanceAmount;
    const uint64_t hardCapInBase = 100 * ONE;
    assetCreationRequest = assetTestHelper.createAssetCreationRequest(baseAsset, syndicate.key.getPublicKey(), "{}",
                                                                      maxIssuanceAmount,0, preIssuedAmount);
    assetTestHelper.createApproveRequest(root, syndicate, assetCreationRequest);

    SaleType crowdfund = SaleType::CROWD_FUNDING;
    SaleType fixedPrice = SaleType::FIXED_PRICE;
    uint32_t allTasks = 0;

    auto account = Account{ SecretKey::random(), 0 };
    auto accountID = account.key.getPublicKey();
    createAccountTestHelper.applyCreateAccountTx(root, account.key.getPublicKey(), AccountType::NOT_VERIFIED);
    auto quoteBalance = BalanceHelperLegacy::Instance()->loadBalance(accountID, defaultQuoteAsset, db, nullptr);
    issuanceHelper.applyCreateIssuanceRequest(root, defaultQuoteAsset, 100 * ONE, quoteBalance->getBalanceID(),
                                                     SecretKey::random().getStrKeyPublic(), &allTasks);

    auto balanceCreationResult = balanceTestHelper.applyManageBalanceTx(account, accountID, baseAsset);
    auto balanceID = balanceCreationResult.success().balanceID;
    auto saleStateData = manageSaleHelper.setSaleState(SaleState::NONE);

     SECTION("Fixed"){
        auto currentTime = testManager->getLedgerManager().getCloseTime();
        auto endTime = currentTime + 1000;

        const auto fixedpriceReq = SaleRequestHelper::createSaleRequest(baseAsset, defaultQuoteAsset, currentTime,
                                                                        endTime, ONE, 100 * ONE, "{}",
                                                                        { saleRequestHelper.createSaleQuoteAsset(defaultQuoteAsset, ONE) },
                                                                        &fixedPrice, &hardCapInBase, SaleState::PROMOTION);

        saleRequestHelper.createApprovedSale(root, syndicate, fixedpriceReq);


        auto sales = SaleHelper::Instance()->loadSalesForOwner(syndicate.key.getPublicKey(), testManager->getDB());
        REQUIRE(sales.size() == 1);
        const auto fixedPriceID = sales[0]->getID();
        manageSaleHelper.applyManageSaleTx(root, fixedPriceID, saleStateData);

        auto manageOfferOp = OfferManager::buildManageOfferOp(balanceCreationResult.success().balanceID, quoteBalance->getBalanceID(),
                                                              true, ONE, ONE, 0, 0, fixedPriceID);
        auto result = offerTestHelper.applyManageOffer(account, manageOfferOp);
        //result.success().offer.offer().offerID;

        testManager->advanceToTime(testManager->getLedgerManager().getCloseTime() + (endTime - currentTime));

        checkSaleStateHelper.applyCheckSaleStateTx(root, fixedPriceID);
        REQUIRE(BalanceHelperLegacy::Instance()->loadBalance(balanceID, db, nullptr)->getAmount() == ONE);

    }
    SECTION("crowd"){
        //crowdfunding

        auto currentTime = testManager->getLedgerManager().getCloseTime();
        auto endTime = currentTime + 1000;

        const auto crowdfundReq = SaleRequestHelper::createSaleRequest(baseAsset, defaultQuoteAsset, currentTime,
                                                                       endTime, ONE, 100 * ONE, "{}",
                                                                       { saleRequestHelper.createSaleQuoteAsset(defaultQuoteAsset, ONE) },
                                                                       &crowdfund, &hardCapInBase, SaleState::PROMOTION);
        saleRequestHelper.createApprovedSale(root, syndicate, crowdfundReq);

        auto sales = SaleHelper::Instance()->loadSalesForOwner(syndicate.key.getPublicKey(), testManager->getDB());
        REQUIRE(sales.size() == 1);

        const auto crowdfundID = sales[0]->getID();

        manageSaleHelper.applyManageSaleTx(root, crowdfundID, saleStateData);

        auto manageOfferOp = OfferManager::buildManageOfferOp(balanceCreationResult.success().balanceID, quoteBalance->getBalanceID(),
                                                              true, ONE, ONE, 0, 0, crowdfundID);
        auto result = offerTestHelper.applyManageOffer(account, manageOfferOp);

        testManager->advanceToTime(testManager->getLedgerManager().getCloseTime() + (endTime - currentTime));

        checkSaleStateHelper.applyCheckSaleStateTx(root, crowdfundID);

        REQUIRE(BalanceHelperLegacy::Instance()->loadBalance(balanceID, db, nullptr)->getAmount() == 100 * ONE);
    }
    SECTION("Hard Path")
    {
        const AssetCode QuoteAsset = "ETH";
        assetCreationRequest = assetTestHelper.createAssetCreationRequest(QuoteAsset, root.key.getPublicKey(), "{}", INT64_MAX,
                                                                          uint32_t(AssetPolicy::BASE_ASSET));
        assetTestHelper.applyManageAssetTx(root, 0, assetCreationRequest);

        const uint64_t hardCap = 12000 * ONE;
        const uint64_t maxIssuanceAmount = 1000 * ONE;
        uint64_t maxAmountToBeSold = maxIssuanceAmount;
        uint64_t amountToInvest = 30 * ONE;
        uint64_t expectedBalanceInBase = 50*ONE;
        uint64_t quoteAssetPrice = 20*ONE;

        ManageAssetPairTestHelper assetPairTestHelper(testManager);
        assetPairTestHelper.createAssetPair(root, QuoteAsset, defaultQuoteAsset, quoteAssetPrice);


        const auto currentTime = testManager->getLedgerManager().getCloseTime();
        const auto endTime = currentTime + 1000;

        const auto fixedpriceReq = SaleRequestHelper::createSaleRequest(baseAsset, defaultQuoteAsset, currentTime,
                                                                        endTime, ONE, hardCap, "{}",
                                                                        { saleRequestHelper.createSaleQuoteAsset(QuoteAsset, ONE)},
                                                                        &fixedPrice, &maxAmountToBeSold, SaleState::PROMOTION);

        saleRequestHelper.createApprovedSale(root, syndicate, fixedpriceReq);


        auto sales = SaleHelper::Instance()->loadSalesForOwner(syndicate.key.getPublicKey(), testManager->getDB());
        REQUIRE(sales.size() == 1);
        const auto fixedPriceID = sales[0]->getID();

        manageSaleHelper.applyManageSaleTx(root, fixedPriceID, saleStateData);
        auto balanceCreationResult = balanceTestHelper.applyManageBalanceTx(account, accountID, QuoteAsset);
        auto quoteBalance = BalanceHelperLegacy::Instance()->loadBalance(accountID, QuoteAsset, db, nullptr);

        issuanceHelper.authorizePreIssuedAmount(root, root.key, QuoteAsset, amountToInvest, root);
        issuanceHelper.applyCreateIssuanceRequest(root, QuoteAsset, amountToInvest, quoteBalance->getBalanceID(),
                                                  SecretKey::random().getStrKeyPublic(), &allTasks);

        auto manageOfferOp = OfferManager::buildManageOfferOp(balanceID, quoteBalance->getBalanceID(),
                                                              true, amountToInvest, ONE, 0, 0, fixedPriceID);
        auto result = offerTestHelper.applyManageOffer(account, manageOfferOp);

        testManager->advanceToTime(testManager->getLedgerManager().getCloseTime() + (endTime - currentTime));

        checkSaleStateHelper.applyCheckSaleStateTx(root, fixedPriceID);
        REQUIRE(BalanceHelperLegacy::Instance()->loadBalance(balanceID, db, nullptr)->getAmount() == expectedBalanceInBase);
    }
}

TEST_CASE("Fixed Price Sale", "[tx][fixedprice]") {
    Config const& cfg = getTestConfig(0, Config::TESTDB_POSTGRESQL);
    VirtualClock clock;
    const Application::pointer appPtr = Application::create(clock, cfg);
    Application& app = *appPtr;
    app.start();
    auto testManager = TestManager::make(app);
    TestManager::upgradeToCurrentLedgerVersion(app);

    auto root = Account{ getRoot(), Salt(0) };

    const AssetCode defaultQuoteAsset = "USD";
    auto assetTestHelper = ManageAssetTestHelper(testManager);
    auto assetCreationRequest = assetTestHelper.createAssetCreationRequest(defaultQuoteAsset, root.key.getPublicKey(), "{}", INT64_MAX,
                                                                           uint32_t(AssetPolicy::BASE_ASSET));
    assetTestHelper.applyManageAssetTx(root, 0, assetCreationRequest);

    CreateAccountTestHelper createAccountTestHelper(testManager);
    SaleRequestHelper saleRequestHelper(testManager);
    IssuanceRequestHelper issuanceHelper(testManager);
    CheckSaleStateHelper checkSaleStateHelper(testManager);
    ManageSaleTestHelper manageSaleHelper(testManager);
    ReviewPromotionUpdateRequestHelper reviewPromotionUpdateHelper(testManager);
    ParticipateInSaleTestHelper participationHelper(testManager);

    auto syndicate = Account{ SecretKey::random(), 0 };
    const auto syndicatePubKey = syndicate.key.getPublicKey();

    createAccountTestHelper.applyCreateAccountTx(root, syndicatePubKey, AccountType::SYNDICATE);
    const AssetCode baseAsset = "BTC";
    const uint64_t maxIssuanceAmount = 20000 * ONE;
    const auto preIssuedAmount = maxIssuanceAmount/2;
    assetCreationRequest = assetTestHelper.createAssetCreationRequest(baseAsset, syndicate.key.getPublicKey(), "{}",
                                                                      maxIssuanceAmount,0, preIssuedAmount);
    assetTestHelper.createApproveRequest(root, syndicate, assetCreationRequest);
    const uint64_t hardCap = 1000 * ONE;
    const uint64_t softCap = hardCap / 4;
    const uint64_t priceInDefaultQuoteAsset = ONE/4;
    const auto currentTime = testManager->getLedgerManager().getCloseTime();
    const auto endTime = currentTime + 1000;
    SaleType saleType = SaleType::FIXED_PRICE;
    uint64_t maxAmountToBeSold = 0;
    bigDivide(maxAmountToBeSold, hardCap, ONE, priceInDefaultQuoteAsset, ROUND_UP);

    const auto saleRequest = SaleRequestHelper::createSaleRequest(baseAsset, defaultQuoteAsset, currentTime,
                                                                 endTime, softCap, hardCap, "{}",
                                                                 { saleRequestHelper.createSaleQuoteAsset(defaultQuoteAsset, ONE) },
                                                                 &saleType, &maxAmountToBeSold, SaleState::PROMOTION);
    saleRequestHelper.createApprovedSale(root, syndicate, saleRequest);
    auto sales = SaleHelper::Instance()->loadSalesForOwner(syndicate.key.getPublicKey(), testManager->getDB());
    REQUIRE(sales.size() == 1);
    const auto saleID = sales[0]->getID();

    auto saleStateData = manageSaleHelper.setSaleState(SaleState::NONE);
    manageSaleHelper.applyManageSaleTx(root, saleID, saleStateData);

    SECTION("Happy path")
    {
        SECTION("Cancel sale")
        {
            //cancel sale
            auto data = manageSaleHelper.createDataForAction(ManageSaleAction::CANCEL);
            manageSaleHelper.applyManageSaleTx(syndicate, saleID, data);
        }

        SECTION("One participant")
        {
            SECTION("Buy whole sale"){
                participationHelper.addNewParticipant(root, saleID, baseAsset, defaultQuoteAsset, hardCap, ONE, 0);
                testManager->advanceToTime(testManager->getLedgerManager().getCloseTime() + (endTime - currentTime));
                // close the sale
                checkSaleStateHelper.applyCheckSaleStateTx(root, saleID);
            }

        }

        SECTION("Many participants")
        {
            const int numberOfParticipants = 10;
            const uint64_t quoteAmount = hardCap / numberOfParticipants;
            const int64_t timeStep = (endTime - currentTime) / numberOfParticipants;
            for (int i = 0; i <= numberOfParticipants - 1; i++)
            {
                participationHelper.addNewParticipant(root, saleID, baseAsset, defaultQuoteAsset, quoteAmount, ONE, 0);
                testManager->advanceToTime(testManager->getLedgerManager().getCloseTime() + timeStep);
            }
            testManager->advanceToTime(endTime + 1);
            checkSaleStateHelper.applyCheckSaleStateTx(root, saleID);
        }

    }

}
