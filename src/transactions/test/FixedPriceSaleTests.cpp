#include <ledger/AssetPairHelper.h>
#include <transactions/FeesManager.h>
#include "main/Application.h"
#include "main/test.h"
#include "TxTests.h"
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
#include "ledger/BalanceHelper.h"
#include "test_helper/ManageBalanceTestHelper.h"
#include "transactions/dex/OfferManager.h"
#include "test_helper/ParticipateInSaleTestHelper.h"
#include "test_helper/ManageSaleTestHelper.h"
#include "test_helper/ReviewPromotionUpdateRequestTestHelper.h"

using namespace stellar;
using namespace stellar::txtest;

typedef std::unique_ptr<Application> appPtr;

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
    CheckSaleStateHelper checkStateHelper(testManager);
    ManageSaleTestHelper manageSaleHelper(testManager);
    ReviewPromotionUpdateRequestHelper reviewPromotionUpdateHelper(testManager);
    ParticipateInSaleTestHelper participationHelper(testManager);

    auto syndicate = Account{ SecretKey::random(), 0 };
    const auto syndicatePubKey = syndicate.key.getPublicKey();

    CreateAccountTestHelper(testManager).applyCreateAccountTx(root, syndicatePubKey, AccountType::SYNDICATE);
    const AssetCode baseAsset = "BTC";
    const uint64_t maxIssuanceAmount = 20000 * ONE;
    const auto preIssuedAmount = maxIssuanceAmount/2;
    assetCreationRequest = assetTestHelper.createAssetCreationRequest(baseAsset, syndicate.key.getPublicKey(), "{}",
                                                                      maxIssuanceAmount,0, preIssuedAmount);
    assetTestHelper.createApproveRequest(root, syndicate, assetCreationRequest);
    const uint64_t hardCap = 1000 * ONE;
    const uint64_t softCap = hardCap / 4;
    const uint64_t priceInDefaultQuoteAsset = 500;
    const auto currentTime = testManager->getLedgerManager().getCloseTime();
    const auto endTime = currentTime + 1000;
    SaleType saleType = SaleType::FIXED_PRICE;
    auto maxAmountToBeSold = hardCap / priceInDefaultQuoteAsset;

    const auto saleRequest = saleRequestHelper.createSaleRequest(baseAsset, defaultQuoteAsset, currentTime,
                                                                 endTime, softCap, hardCap, "{}",
                                                                 { saleRequestHelper.createSaleQuoteAsset(defaultQuoteAsset, ONE) },
                                                                 &saleType, &maxAmountToBeSold, SaleState::PROMOTION);
    saleRequestHelper.createApprovedSale(root, syndicate, saleRequest);
    auto sales = SaleHelper::Instance()->loadSalesForOwner(syndicate.key.getPublicKey(), testManager->getDB());
    REQUIRE(sales.size() == 1);
    const auto saleID = sales[0]->getID();
    auto saleTestHelper = ManageSaleTestHelper(testManager);
    auto saleStateData = saleTestHelper.setSaleState(SaleState::NONE);
    saleTestHelper.applyManageSaleTx(root, saleID, saleStateData);


    SECTION("Happy path")
    {
        SECTION("Cansel sale")
        {
            //cancel sale
            auto data = manageSaleHelper.createDataForAction(ManageSaleAction::CANCEL);
            manageSaleHelper.applyManageSaleTx(syndicate, saleID, data);
        }

        SECTION("One participant")
        {
            // create participant
            Account participant = Account{ SecretKey::random(), Salt(0) };
            AccountID participantID = participant.key.getPublicKey();
            createAccountTestHelper.applyCreateAccountTx(root, participantID, AccountType::GENERAL);

            // create base balance for participant:
            auto manageBalanceRes = ManageBalanceTestHelper(testManager).applyManageBalanceTx(participant, participantID, baseAsset);
            BalanceID baseBalance = manageBalanceRes.success().balanceID;
            Database& db = testManager->getDB();
            BalanceID quoteBalance = BalanceHelper::Instance()->loadBalance(participantID, defaultQuoteAsset, db,
                                                                            nullptr)->getBalanceID();

            // pre-issue quote amount
            issuanceHelper.authorizePreIssuedAmount(root, root.key, defaultQuoteAsset, INT64_MAX, root);

            // fund participant with quote asset
            uint64_t quoteBalanceAmount = INT64_MAX;
            uint32_t allTasks = 0;
            issuanceHelper.applyCreateIssuanceRequest(root, defaultQuoteAsset, quoteBalanceAmount, quoteBalance,
                                                      SecretKey::random().getStrKeyPublic(), &allTasks);

            SECTION("Buy whole sale"){
                auto manageOffer = OfferManager::buildManageOfferOp(baseBalance, quoteBalance, true, hardCap,
                                                                    ONE, 0, 0, saleID);

                participationHelper.applyManageOffer(participant, manageOffer);

                // close the sale
                CheckSaleStateHelper(testManager).applyCheckSaleStateTx(root, saleID);
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
            checkStateHelper.applyCheckSaleStateTx(root, saleID);
        }

    }

}
