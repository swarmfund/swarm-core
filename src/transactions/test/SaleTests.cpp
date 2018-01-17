// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0
#include "main/Application.h"
#include "ledger/LedgerManager.h"
#include "main/test.h"
#include "TxTests.h"
#include "util/Timer.h"
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
        true, baseAssetAmount, price, 0, fee, saleID);
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

    const AssetCode quoteAsset = "USD";
    auto assetTestHelper = ManageAssetTestHelper(testManager);
    auto assetCreationRequest = assetTestHelper.createAssetCreationRequest(quoteAsset, root.key.getPublicKey(), "{}", INT64_MAX, uint32_t(AssetPolicy::BASE_ASSET));
    assetTestHelper.applyManageAssetTx(root, 0, assetCreationRequest);
    CreateAccountTestHelper createAccountTestHelper(testManager);
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
        auto saleRequestHelper = SaleRequestHelper(testManager);
        const auto currentTime = testManager->getLedgerManager().getCloseTime();
        const auto price = 2 * ONE;
        const auto hardCap = bigDivide(maxIssuanceAmount, price, ONE, ROUND_DOWN);
        const auto softCap = hardCap / 2;
        const auto saleRequest = saleRequestHelper.createSaleRequest(baseAsset, quoteAsset, currentTime, currentTime + 1000, price, softCap, hardCap, "{}");
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
                CheckSaleStateHelper(testManager).applyCheckSaleStateTx(root, CheckSaleStateResultCode::NO_SALES_FOUND);
            }
            // hardcap is not reached, so no sale to close
            CheckSaleStateHelper(testManager).applyCheckSaleStateTx(root, CheckSaleStateResultCode::NO_SALES_FOUND);
            // TODO close ledger after end time of the sale and check sale state
        }
    }
}
