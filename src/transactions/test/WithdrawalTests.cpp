// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0
#include "main/Application.h"
#include "ledger/LedgerManager.h"
#include "main/Config.h"
#include "main/test.h"
#include "lib/catch.hpp"
#include "util/Logging.h"
#include "TxTests.h"
#include "util/Timer.h"
#include "ledger/LedgerDelta.h"
#include "crypto/SHA.h"
#include "test_helper/TestManager.h"
#include "test_helper/Account.h"
#include "test_helper/ManageAssetTestHelper.h"
#include "test_helper/IssuanceRequestHelper.h"
#include "ledger/BalanceHelper.h"
#include "test_helper/WithdrawRequestHelper.h"
#include "test_helper/ReviewWithdrawalRequestHelper.h"

using namespace stellar;
using namespace stellar::txtest;

typedef std::unique_ptr<Application> appPtr;


TEST_CASE("Manage forfeit request", "[tx][withdraw]")
{

    Config const& cfg = getTestConfig(0, Config::TESTDB_POSTGRESQL);
    VirtualClock clock;
    Application::pointer appPtr = Application::create(clock, cfg);
    Application& app = *appPtr;
    app.start();
    auto testManager = TestManager::make(app);

    auto root = Account{ getRoot(), Salt(0) };
    auto issuanceHelper = IssuanceRequestHelper(testManager);
    // create asset and make it withdrawable
    const AssetCode asset = "USD";
    const uint64_t preIssuedAmount = 10000 * ONE;
    issuanceHelper.createAssetWithPreIssuedAmount(root, asset, preIssuedAmount, root);
    auto assetHelper = ManageAssetTestHelper(testManager);
    assetHelper.updateAsset(root, asset, root, static_cast<uint32_t>(AssetPolicy::BASE_ASSET) | static_cast<uint32_t>(AssetPolicy::WITHDRAWABLE));

    // create account wich will withdraw
    auto withdrawerKP = SecretKey::random();
    applyCreateAccountTx(testManager->getApp(), root.key, withdrawerKP, root.getNextSalt(), AccountType::GENERAL);
    auto withdrawer = Account{ withdrawerKP, Salt(0) };
    auto withdrawerBalance = BalanceHelper::Instance()->loadBalance(withdrawerKP.getPublicKey(), asset, testManager->getDB(), nullptr);
    REQUIRE(!!withdrawerBalance);
    issuanceHelper.applyCreateIssuanceRequest(root, asset, preIssuedAmount, withdrawerBalance->getBalanceID(), "RANDOM ISSUANCE REFERENCE");

    SECTION("Happy path")
    {
        // create asset in which we'll withdraw
        const AssetCode withdrawDestAsset = "BTC";
        assetHelper.createAsset(root, root.key, withdrawDestAsset, root, 0);
        const uint64_t price = 2000 * ONE;
        applyManageAssetPairTx(testManager->getApp(), root.key, 0, withdrawDestAsset, asset, price, 0, 0, 0);
        uint64_t amountToWithdraw = 1000 * ONE;
        withdrawerBalance = BalanceHelper::Instance()->loadBalance(withdrawerKP.getPublicKey(), asset, testManager->getDB(), nullptr);
        REQUIRE(withdrawerBalance->getAmount() >= amountToWithdraw);
        const uint64_t expectedAmountInDestAsset = 0.5 * ONE;
        const auto withdrawRequest = WithdrawRequestHelper::createWithdrawRequest(withdrawerBalance->getBalanceID(), amountToWithdraw,
            Fee{ 0, 0 }, "{}", withdrawDestAsset, expectedAmountInDestAsset);
        auto withdrawResult = WithdrawRequestHelper(testManager).applyCreateWithdrawRequest(withdrawer, withdrawRequest);
        ReviewWithdrawRequestHelper(testManager).applyReviewRequestTx(root, withdrawResult.success().requestID, ReviewRequestOpAction::APPROVE,"");
    }
}
