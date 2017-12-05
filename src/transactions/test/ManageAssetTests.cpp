// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0
#include <transactions/test/test_helper/IssuanceRequestHelper.h>
#include "main/Config.h"
#include "main/test.h"
#include "lib/catch.hpp"
#include "TxTests.h"
#include "test_helper/TestManager.h"
#include "test_helper/ManageAssetHelper.h"
#include "test_helper/ReviewAssetRequestHelper.h"
#include "test_helper/IssuanceRequestHelper.h"

using namespace stellar;
using namespace txtest;

void testManageAssetHappyPath(TestManager::pointer testManager,
                              Account& account, Account& root);


TEST_CASE("manage asset", "[tx][manage_asset]")
{
    auto const& cfg = getTestConfig(0, Config::TESTDB_POSTGRESQL);
    VirtualClock clock;
    const auto appPtr = Application::create(clock, cfg);
    auto& app = *appPtr;
    app.start();
    auto testManager = TestManager::make(app);

    auto root = Account{getRoot(), Salt(0)};

    SECTION("Root happy path")
    {
        testManageAssetHappyPath(testManager, root, root);
    }
    SECTION("Cancel asset request")
    {
        auto manageAssetHelper = ManageAssetHelper(testManager);
        SECTION("Invalid ID")
        {
            manageAssetHelper.applyManageAssetTx(root, 0,
                                                 manageAssetHelper.
                                                 createCancelRequest(),
                                                 ManageAssetResultCode::
                                                 REQUEST_NOT_FOUND);
        }
        SECTION("Request not found")
        {
            manageAssetHelper.applyManageAssetTx(root, 12,
                                                 manageAssetHelper.
                                                 createCancelRequest(),
                                                 ManageAssetResultCode::
                                                 REQUEST_NOT_FOUND);
        }
        SECTION("Request has invalid type")
        {
            // 1. create asset
            // 2. create pre issuance request for it
            // 3. try to cancel it with asset request
            const AssetCode asset = "USDT";
            manageAssetHelper.createAsset(root, root.key, asset, root);
            auto issuanceHelper = IssuanceRequestHelper(testManager);
            auto requestResult = issuanceHelper.
                applyCreatePreIssuanceRequest(root, root.key, asset, 10000,
                                              SecretKey::random().
                                              getStrKeyPublic());
            const auto cancelRequest = manageAssetHelper.createCancelRequest();
            manageAssetHelper.
                applyManageAssetTx(root, requestResult.success().requestID,
                                   cancelRequest,
                                   ManageAssetResultCode::REQUEST_NOT_FOUND);
        }
    }
    SECTION("Asset creation request")
    {
        auto manageAssetHelper = ManageAssetHelper(testManager);
        SECTION("Invalid asset code")
        {
            const auto request = manageAssetHelper.
                createAssetCreationRequest("USD S", "USDS",
                                           root.key.getPublicKey(), "", "", 100,
                                           0);
            manageAssetHelper.applyManageAssetTx(root, 0, request,
                                                 ManageAssetResultCode::
                                                 INVALID_CODE);
        }
        SECTION("Invalid asset name")
        {
            const auto request = manageAssetHelper.
                createAssetCreationRequest("USDS", "", root.key.getPublicKey(),
                                           "", "", 100, 0);
            manageAssetHelper.applyManageAssetTx(root, 0, request,
                                                 ManageAssetResultCode::
                                                 INVALID_NAME);
        }
        SECTION("Invalid policies")
        {
            const auto request = manageAssetHelper.
                createAssetCreationRequest("USDS", "USD S",
                                           root.key.getPublicKey(), "", "", 100,
                                           UINT32_MAX);
            manageAssetHelper.applyManageAssetTx(root, 0, request,
                                                 ManageAssetResultCode::
                                                 INVALID_POLICIES);
        }
        SECTION("Trying to update non existsing request")
        {
            const auto request = manageAssetHelper.
                createAssetCreationRequest("USDS", "USDS",
                                           root.key.getPublicKey(), "", "", 100,
                                           0);
            manageAssetHelper.applyManageAssetTx(root, 1, request,
                                                 ManageAssetResultCode::
                                                 REQUEST_NOT_FOUND);
        }
        SECTION("Trying to create request for same asset twice")
        {
            const AssetCode assetCode = "EURT";
            const auto request = manageAssetHelper.
                createAssetCreationRequest(assetCode, "USDS",
                                           root.key.getPublicKey(), "", "", 100,
                                           0);
            manageAssetHelper.applyManageAssetTx(root, 0, request);
            manageAssetHelper.applyManageAssetTx(root, 0, request,
                                                 ManageAssetResultCode::
                                                 REQUEST_ALREADY_EXISTS);
        }
        SECTION("Trying to create asset which is already exist")
        {
            const AssetCode assetCode = "EUR";
            manageAssetHelper.createAsset(root, root.key, assetCode, root);
            const auto request = manageAssetHelper.
                createAssetCreationRequest(assetCode, "USDS",
                                           root.key.getPublicKey(), "", "", 100,
                                           0);
            manageAssetHelper.applyManageAssetTx(root, 0, request,
                                                 ManageAssetResultCode::
                                                 ASSET_ALREADY_EXISTS);
        }
    }
    SECTION("Asset update request")
    {
        auto manageAssetHelper = ManageAssetHelper(testManager);
        SECTION("Invalid asset code")
        {
            const auto request = manageAssetHelper.
                createAssetUpdateRequest("USD S", "Long desciption'as'd.", "",
                                         0);
            manageAssetHelper.applyManageAssetTx(root, 0, request,
                                                 ManageAssetResultCode::
                                                 INVALID_CODE);
        }
        SECTION("Invalid asset policies")
        {
            const auto request = manageAssetHelper.
                createAssetUpdateRequest("USDS", "Long desciption'as'd.", "",
                                         UINT32_MAX);
            manageAssetHelper.applyManageAssetTx(root, 0, request,
                                                 ManageAssetResultCode::
                                                 INVALID_POLICIES);
        }
        SECTION("Trying to update non existsing request")
        {
            const auto request = manageAssetHelper.
                createAssetUpdateRequest("USDS", "Long desciption'as'd.", "", 0);
            manageAssetHelper.applyManageAssetTx(root, 12, request, ManageAssetResultCode::REQUEST_NOT_FOUND);
        }
        SECTION("Trying to update not my asset")
        {
            // create asset by syndicate
            auto syndicate = Account{SecretKey::random(), Salt(0)};
            applyCreateAccountTx(testManager->getApp(), root.key, syndicate.key,
                                 0, AccountType::SYNDICATE);
            const AssetCode assetCode = "BTC";
            manageAssetHelper.createAsset(syndicate, syndicate.key, assetCode,
                                          root);
            // try to update with root
            const auto request = manageAssetHelper.
                createAssetUpdateRequest(assetCode, "Long desciption'as'd.", "",
                                         0);
            manageAssetHelper.applyManageAssetTx(root, 0, request,
                                                 ManageAssetResultCode::
                                                 ASSET_NOT_FOUND);
        }
    }
}


void testManageAssetHappyPath(TestManager::pointer testManager,
                              Account& account, Account& root)
{
    SECTION("Can create asset")
    {
        auto preissuedSigner = SecretKey::random();
        auto manageAssetHelper = ManageAssetHelper(testManager);
        const AssetCode assetCode = "EURT";
        auto creationRequest = manageAssetHelper.
            createAssetCreationRequest(assetCode, "New USD token",
                                       preissuedSigner.getPublicKey(),
                                       "Description can be quiete long",
                                       "https://testusd.usd", 0, 0);
        auto creationResult = manageAssetHelper.applyManageAssetTx(account, 0,
                                                                   creationRequest);
        SECTION("Can cancel creation request")
        {
            manageAssetHelper.applyManageAssetTx(account,
                                                 creationResult.success().
                                                                requestID,
                                                 manageAssetHelper.
                                                 createCancelRequest());
        }
        SECTION("Can update existing request")
        {
            creationRequest.createAsset().code = "USDT";
            auto updateResult = manageAssetHelper.applyManageAssetTx(account,
                                                                     creationResult
                                                                     .success().
                                                                      requestID,
                                                                     creationRequest);
            REQUIRE(updateResult.success().requestID == creationResult.success()
                .requestID);
        }
        SECTION("Given approved asset")
        {
            auto& delta = testManager->getLedgerDelta();
            auto approvingRequest = ReviewableRequestFrame::
                loadRequest(creationResult.success().requestID,
                            testManager->getDB(), &delta);
            REQUIRE(approvingRequest);
            auto reviewRequetHelper = ReviewAssetRequestHelper(testManager);
            reviewRequetHelper.applyReviewRequestTx(root, approvingRequest->
                                                    getRequestID(),
                                                    approvingRequest->getHash(),
                                                    approvingRequest->getType(),
                                                    ReviewRequestOpAction::
                                                    APPROVE, "");
            SECTION("Can update asset")
            {
                const auto updateRequestBody = manageAssetHelper.
                    createAssetUpdateRequest(assetCode,
                                             "Updated token descpition",
                                             "https://updatedlink.token", 0);
                auto updateResult = manageAssetHelper.
                    applyManageAssetTx(account, 0, updateRequestBody);
                approvingRequest = ReviewableRequestFrame::
                    loadRequest(updateResult.success().requestID,
                                testManager->getDB(), &delta);
                REQUIRE(approvingRequest);
                reviewRequetHelper.applyReviewRequestTx(root, approvingRequest->
                                                        getRequestID(),
                                                        approvingRequest->
                                                        getHash(),
                                                        approvingRequest->
                                                        getType(),
                                                        ReviewRequestOpAction::
                                                        APPROVE, "");
            }
        }
    }
}
