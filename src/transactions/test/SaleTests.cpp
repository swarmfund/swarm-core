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
    SECTION("Happy path")
    {
        auto syndicate = Account{ SecretKey::random(), 0 };
        const auto syndicatePubKey = syndicate.key.getPublicKey();
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
        SECTION("Reached hard cap")
        {
            auto accountTestHelper = CreateAccountTestHelper(testManager);
            auto issuanceHelper = IssuanceRequestHelper(testManager);
            issuanceHelper.authorizePreIssuedAmount(root, root.key, quoteAsset, hardCap, root);
            auto balanceHelper = ManageBalanceTestHelper(testManager);
            const int numberOfParticipants = 10;
            auto participateInSaleHelper = ParticipateInSaleTestHelper(testManager);
            auto sales = SaleHelper::Instance()->loadSales(baseAsset, quoteAsset, testManager->getDB());
            REQUIRE(sales.size() == 1);
            auto saleID = sales[0]->getID();
            for (auto i = 0; i < numberOfParticipants; i++)
            {
                auto account = Account{ SecretKey::random(), 0 };
                accountTestHelper.applyCreateAccountTx(root, account.key.getPublicKey(), AccountType::NOT_VERIFIED);
                auto quoteBalance = BalanceHelper::Instance()->loadBalance(account.key.getPublicKey(), quoteAsset, testManager->getDB(), nullptr);
                REQUIRE(!!quoteBalance);
                const auto quoteAssetAmount = hardCap / numberOfParticipants;
                issuanceHelper.applyCreateIssuanceRequest(root, quoteAsset, quoteAssetAmount, quoteBalance->getBalanceID(),
                    SecretKey::random().getStrKeyPublic());
                auto accountID = account.key.getPublicKey();
                auto balanceCreationResult = balanceHelper.applyManageBalanceTx(account, accountID, baseAsset);
                const auto baseAssetAmount = bigDivide(quoteAssetAmount, ONE, price, ROUND_UP);
                auto manageOfferOp = OfferManager::buildManageOfferOp(balanceCreationResult.success().balanceID, quoteBalance->getBalanceID(),
                    true, baseAssetAmount, price, 0, 0, saleID);
                participateInSaleHelper.applyManageOffer(account, manageOfferOp);
                if (i < numberOfParticipants - 1)
                {
                    CheckSaleStateHelper(testManager).applyCheckSaleStateTx(root, CheckSaleStateResultCode::NO_SALES_FOUND);
                }
            }

            CheckSaleStateHelper(testManager).applyCheckSaleStateTx(root);
        }
    }
}
