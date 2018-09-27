// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include <ledger/AccountHelper.h>
#include <ledger/FeeHelper.h>
#include <transactions/test/test_helper/ManageLimitsTestHelper.h>
#include "ledger/AccountLimitsHelper.h"
#include "ledger/BalanceHelperLegacy.h"
#include "main/test.h"
#include "TxTests.h"
#include "crypto/SHA.h"
#include "test_helper/CreateAccountTestHelper.h"
#include "test_helper/IssuanceRequestHelper.h"
#include "test_helper/LimitsUpdateRequestHelper.h"
#include "test_helper/ManageAssetTestHelper.h"
#include "test_helper/ManageAssetPairTestHelper.h"
#include "test_helper/ReviewLimitsUpdateRequestHelper.h"
#include "test/test_marshaler.h"

using namespace stellar;
using namespace stellar::txtest;

typedef std::unique_ptr<Application> appPtr;

TEST_CASE("limits update", "[tx][limits_update]")
{
    Config const& cfg = getTestConfig(0, Config::TESTDB_POSTGRESQL);
    VirtualClock clock;
    Application::pointer appPtr = Application::create(clock, cfg);
    Application& app = *appPtr;
    app.start();
    auto testManager = TestManager::make(app);

    upgradeToCurrentLedgerVersion(app);

    auto root = Account{ getRoot(), Salt(0) };
    auto createAccountTestHelper = CreateAccountTestHelper(testManager);
    auto limitsUpdateRequestHelper = LimitsUpdateRequestHelper(testManager);
    auto reviewLimitsUpdateHelper = ReviewLimitsUpdateRequestHelper(testManager);
    auto manageLimitsTestHelper = ManageLimitsTestHelper(testManager);

    // create requestor
    auto requestor = Account{ SecretKey::random(), Salt(0) };
    AccountID requestorID = requestor.key.getPublicKey();
    createAccountTestHelper.applyCreateAccountTx(root, requestorID,
                                                 AccountType::GENERAL);

    // set default limits for review request
    reviewLimitsUpdateHelper.initializeLimits(requestorID);

    // prepare data for request
    std::string documentData = "{}";

    // create LimitsUpdateRequest
    auto limitsUpdateRequest = limitsUpdateRequestHelper.createLimitsUpdateRequest(documentData);
    auto limitsUpdateResult = limitsUpdateRequestHelper.applyCreateLimitsUpdateRequest(requestor, limitsUpdateRequest);

    SECTION("Happy path")
    {
        SECTION("Approve")
        {
            reviewLimitsUpdateHelper.applyReviewRequestTx(root, limitsUpdateResult.success().manageLimitsRequestID,
                                                          ReviewRequestOpAction::APPROVE, "");
        }
        SECTION("Reject")
        {
            reviewLimitsUpdateHelper.applyReviewRequestTx(root, limitsUpdateResult.success().manageLimitsRequestID,
                                                          ReviewRequestOpAction::REJECT, "Invalid document");
        }
        SECTION("Permanent reject")
        {
            reviewLimitsUpdateHelper.applyReviewRequestTx(root, limitsUpdateResult.success().manageLimitsRequestID,
                                                          ReviewRequestOpAction::PERMANENT_REJECT, "Invalid document");
        }
        SECTION("Approve for account with limits")
        {
            auto accountWithLimits = Account {SecretKey::random(), Salt(0)};
            AccountID accountWithoutLimitsID = accountWithLimits.key.getPublicKey();
            createAccountTestHelper.applyCreateAccountTx(root, accountWithoutLimitsID,
                                                         AccountType::GENERAL);
            ManageLimitsOp manageLimitsOp;
            manageLimitsOp.details.action(ManageLimitsAction::CREATE);
            manageLimitsOp.details.limitsCreateDetails().accountID.activate() = accountWithoutLimitsID;
            manageLimitsOp.details.limitsCreateDetails().assetCode = "USD";
            manageLimitsOp.details.limitsCreateDetails().statsOpType = StatsOpType::PAYMENT_OUT;
            manageLimitsOp.details.limitsCreateDetails().isConvertNeeded = false;
            manageLimitsOp.details.limitsCreateDetails().dailyOut = 10;
            manageLimitsOp.details.limitsCreateDetails().weeklyOut = 20;
            manageLimitsOp.details.limitsCreateDetails().monthlyOut = 30;
            manageLimitsOp.details.limitsCreateDetails().annualOut = 50;
            manageLimitsTestHelper.applyManageLimitsTx(root, manageLimitsOp);

            std::string documentDataOfAccountWithLimits = "{\n \"a\": \"I have a lot of money\" \n}";

            limitsUpdateRequest = limitsUpdateRequestHelper.createLimitsUpdateRequest(documentDataOfAccountWithLimits);
            limitsUpdateResult = limitsUpdateRequestHelper.applyCreateLimitsUpdateRequest(accountWithLimits,
                                                                                          limitsUpdateRequest);

            reviewLimitsUpdateHelper.applyReviewRequestTx(root, limitsUpdateResult.success().manageLimitsRequestID,
                                                          ReviewRequestOpAction::APPROVE, "");
        }
        SECTION("Update limits update request")
        {
            std::string newDocumentData = "{\n \"a\": \"New document data\" \n}";
            limitsUpdateRequest.ext.details() = newDocumentData;
            uint64_t limitsUpdateRequestID = limitsUpdateResult.success().manageLimitsRequestID;
            limitsUpdateResult = limitsUpdateRequestHelper.applyCreateLimitsUpdateRequest(requestor,
                                                                                          limitsUpdateRequest,
                                                                                          &limitsUpdateRequestID);
        }
    }

    uint64_t requestID = 0;

    SECTION("Invalid details")
    {
        limitsUpdateRequest.ext.details() = "Some document data, huge data, very huge data to check convert to string64"
                       " when get reference to write to database information about request";
        limitsUpdateResult = limitsUpdateRequestHelper.applyCreateLimitsUpdateRequest(requestor, limitsUpdateRequest,
                                                                                      &requestID,
                                               CreateManageLimitsRequestResultCode::INVALID_DETAILS);
    }

    SECTION("Update non existing request")
    {
        requestID = 42;
        limitsUpdateResult = limitsUpdateRequestHelper.applyCreateLimitsUpdateRequest(requestor, limitsUpdateRequest,
                                                                                      &requestID,
        CreateManageLimitsRequestResultCode::MANAGE_LIMITS_REQUEST_NOT_FOUND);

    }

    SECTION("Create same request for second time")
    {
        limitsUpdateResult = limitsUpdateRequestHelper.applyCreateLimitsUpdateRequest(requestor, limitsUpdateRequest,
                                                                                      &requestID,
                             CreateManageLimitsRequestResultCode::MANAGE_LIMITS_REQUEST_REFERENCE_DUPLICATION);
    }
    SECTION("Approve and create same request for second time")
    {
        reviewLimitsUpdateHelper.applyReviewRequestTx(root, limitsUpdateResult.success().manageLimitsRequestID,
                                                      ReviewRequestOpAction::APPROVE, "");
        limitsUpdateResult = limitsUpdateRequestHelper.applyCreateLimitsUpdateRequest(requestor, limitsUpdateRequest,
                                                                                      &requestID,
                             CreateManageLimitsRequestResultCode::MANAGE_LIMITS_REQUEST_REFERENCE_DUPLICATION);
    }
}