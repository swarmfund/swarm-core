// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0
#include <transactions/test/test_helper/TestManager.h>
#include <transactions/test/test_helper/ManageAssetPairTestHelper.h>
#include <transactions/test/test_helper/ManageAssetTestHelper.h>
#include "main/Application.h"
#include "util/Timer.h"
#include "main/Config.h"
#include "main/test.h"
#include "TxTests.h"
#include "ledger/LedgerDelta.h"
#include "test/test_marshaler.h"

using namespace stellar;
using namespace stellar::txtest;

typedef std::unique_ptr<Application> appPtr;

TEST_CASE("manage asset pair", "[tx][manage_asset_pair]")
{
    Config const& cfg = getTestConfig(0, Config::TESTDB_POSTGRESQL);
    VirtualClock clock;
    Application::pointer appPtr = Application::create(clock, cfg);
    Application& app = *appPtr;
    app.start();
    auto testManager = TestManager::make(app);

    auto assetPairTestHelper = ManageAssetPairTestHelper(testManager);

    // set up world
    auto root = Account{getRoot(), 0};

    SECTION("Basic")
    {
        AssetCode base = "EUR";
        const AssetCode quote = "UAH";
        int64_t physicalPrice = 12;
        int64_t physicalPriceCorrection = 95 * ONE;
        int64_t maxPriceStep = 5 * ONE;
        int32_t policies = getAnyAssetPairPolicy();
        SECTION("Invalid asset")
        {
            base = "''asd";
            assetPairTestHelper.applyManageAssetPairTx(root, base, quote,
                                                       physicalPrice,
                                                       physicalPriceCorrection,
                                                       maxPriceStep, policies,
                                                       ManageAssetPairAction::
                                                       CREATE, nullptr,
                                                       ManageAssetPairResultCode
                                                       ::INVALID_ASSET);
            assetPairTestHelper.applyManageAssetPairTx(root, quote, base,
                                                       physicalPrice,
                                                       physicalPriceCorrection,
                                                       maxPriceStep, policies,
                                                       ManageAssetPairAction::
                                                       CREATE, nullptr,
                                                       ManageAssetPairResultCode
                                                       ::INVALID_ASSET);
        }
        SECTION("Invalid action")
        {
            assetPairTestHelper.applyManageAssetPairTx(root, quote, base,
                                                       physicalPrice,
                                                       physicalPriceCorrection,
                                                       maxPriceStep, policies,
                                                       ManageAssetPairAction(1201),
                                                       nullptr,
                                                       ManageAssetPairResultCode
                                                       ::INVALID_ACTION);
        }
        SECTION("Invalid physical price")
        {
            assetPairTestHelper.applyManageAssetPairTx(root, quote, base,
                                                       -physicalPrice,
                                                       physicalPriceCorrection,
                                                       maxPriceStep, policies,
                                                       ManageAssetPairAction::
                                                       CREATE, nullptr,
                                                       ManageAssetPairResultCode
                                                       ::MALFORMED);
        }
        SECTION("Invalid physical price correction")
        {
            assetPairTestHelper.applyManageAssetPairTx(root, quote, base,
                                                       physicalPrice,
                                                       -physicalPriceCorrection,
                                                       maxPriceStep, policies,
                                                       ManageAssetPairAction::
                                                       CREATE, nullptr,
                                                       ManageAssetPairResultCode
                                                       ::MALFORMED);
        }
        SECTION("Invalid max price step")
        {
            assetPairTestHelper.applyManageAssetPairTx(root, quote, base,
                                                       physicalPrice,
                                                       physicalPriceCorrection,
                                                       -maxPriceStep, policies,
                                                       ManageAssetPairAction::
                                                       CREATE, nullptr,
                                                       ManageAssetPairResultCode
                                                       ::MALFORMED);
        }
        SECTION("Invalid policies")
        {
            assetPairTestHelper.applyManageAssetPairTx(root, quote, base,
                                                       physicalPrice,
                                                       physicalPriceCorrection,
                                                       maxPriceStep, -policies,
                                                       ManageAssetPairAction::
                                                       CREATE, nullptr,
                                                       ManageAssetPairResultCode
                                                       ::INVALID_POLICIES);
        }
        SECTION("Asset does not exists")
        {
            assetPairTestHelper.applyManageAssetPairTx(root, quote, base,
                                                       physicalPrice,
                                                       physicalPriceCorrection,
                                                       maxPriceStep, policies,
                                                       ManageAssetPairAction::
                                                       CREATE, nullptr,
                                                       ManageAssetPairResultCode
                                                       ::ASSET_NOT_FOUND);
        }
        SECTION("Asset created")
        {
            auto assetTestHelper = ManageAssetTestHelper(testManager);
            assetTestHelper.createAsset(root, root.key, base, root, 0);
            assetTestHelper.createAsset(root, root.key, quote, root, 0);
            SECTION("Pair already exists")
            {
                assetPairTestHelper.applyManageAssetPairTx(root, quote, base,
                                                           physicalPrice,
                                                           physicalPriceCorrection,
                                                           maxPriceStep,
                                                           policies);
                assetPairTestHelper.applyManageAssetPairTx(root, quote, base,
                                                           physicalPrice,
                                                           physicalPriceCorrection,
                                                           maxPriceStep,
                                                           policies,
                                                           ManageAssetPairAction
                                                           ::CREATE, nullptr,
                                                           ManageAssetPairResultCode
                                                           ::ALREADY_EXISTS);
                // reverse pair already exists
                assetPairTestHelper.applyManageAssetPairTx(root, base, quote,
                                                           physicalPrice,
                                                           physicalPriceCorrection,
                                                           maxPriceStep,
                                                           policies,
                                                           ManageAssetPairAction
                                                           ::CREATE, nullptr,
                                                           ManageAssetPairResultCode
                                                           ::ALREADY_EXISTS);
            }
            SECTION("Pair does not exists")
            {
                assetPairTestHelper.applyManageAssetPairTx(root, quote, base,
                                                           physicalPrice,
                                                           physicalPriceCorrection,
                                                           maxPriceStep,
                                                           policies,
                                                           ManageAssetPairAction
                                                           ::UPDATE_POLICIES,
                                                           nullptr,
                                                           ManageAssetPairResultCode
                                                           ::NOT_FOUND);
                assetPairTestHelper.applyManageAssetPairTx(root, quote, base,
                                                           physicalPrice,
                                                           physicalPriceCorrection,
                                                           maxPriceStep,
                                                           policies,
                                                           ManageAssetPairAction
                                                           ::UPDATE_PRICE,
                                                           nullptr,
                                                           ManageAssetPairResultCode
                                                           ::NOT_FOUND);
            }
            SECTION("Create -> update policies -> update price")
            {
                // create
                assetPairTestHelper.applyManageAssetPairTx(root, quote, base,
                                                           physicalPrice,
                                                           physicalPriceCorrection,
                                                           maxPriceStep,
                                                           policies,
                                                           ManageAssetPairAction
                                                           ::CREATE);
                // update policies
                physicalPriceCorrection = physicalPriceCorrection + 100 * ONE;
                maxPriceStep = maxPriceStep + 1 * ONE;
                policies = static_cast<int32_t>(AssetPairPolicy::TRADEABLE_SECONDARY_MARKET) |
                           static_cast<int32_t>(AssetPairPolicy::
                               CURRENT_PRICE_RESTRICTION);
                assetPairTestHelper.applyManageAssetPairTx(root, quote, base,
                                                           physicalPrice,
                                                           physicalPriceCorrection,
                                                           maxPriceStep,
                                                           policies,
                                                           ManageAssetPairAction
                                                           ::UPDATE_POLICIES);
                // update price
                physicalPrice = physicalPrice + 125 * ONE;
                assetPairTestHelper.applyManageAssetPairTx(root, quote, base,
                                                           physicalPrice,
                                                           physicalPriceCorrection,
                                                           maxPriceStep,
                                                           policies,
                                                           ManageAssetPairAction
                                                           ::UPDATE_PRICE);
            }
        }
    }
}
