// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include <ledger/AccountHelper.h>
#include <ledger/FeeHelper.h>
#include <transactions/test/test_helper/ManageLimitsTestHelper.h>
#include "ledger/AccountLimitsHelper.h"
#include "ledger/BalanceHelper.h"
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
    std::string documentData = "Some document data";
    stellar::Hash documentHash = Hash(sha256(documentData));

    // create LimitsUpdateRequest
    auto limitsUpdateRequest = limitsUpdateRequestHelper.createLimitsUpdateRequest(documentHash);
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
                                                          ReviewRequestOpAction::PERMANENT_REJECT, "Invalid document");
        }
        SECTION("Approve for account with limits")
        {
            auto accountWithoutLimits = Account {SecretKey::random(), Salt(0)};
            AccountID accountWithoutLimitsID = accountWithoutLimits.key.getPublicKey();
            createAccountTestHelper.applyCreateAccountTx(root, accountWithoutLimitsID,
                                                         AccountType::GENERAL);
            ManageLimitsOp manageLimitsOp;
            manageLimitsOp.accountID.activate() = accountWithoutLimitsID;
            manageLimitsOp.assetCode = "USD";
            manageLimitsOp.statsOpType = StatsOpType::PAYMENT_OUT;
            manageLimitsOp.isConvertNeeded = false;
            manageLimitsOp.isDelete = false;
            manageLimitsOp.dailyOut = 10;
            manageLimitsOp.weeklyOut = 20;
            manageLimitsOp.monthlyOut = 30;
            manageLimitsOp.annualOut = 50;
            manageLimitsTestHelper.applyManageLimitsTx(root, manageLimitsOp);

            std::string documentDataOfAccountWithoutLimits = "Some other document data";
            stellar::Hash documentHashOfAccountWithoutLimits = Hash(sha256(documentDataOfAccountWithoutLimits));

            limitsUpdateRequest = limitsUpdateRequestHelper.createLimitsUpdateRequest(documentHashOfAccountWithoutLimits);
            limitsUpdateResult = limitsUpdateRequestHelper.applyCreateLimitsUpdateRequest(accountWithoutLimits,
                                                                                          limitsUpdateRequest);

            reviewLimitsUpdateHelper.applyReviewRequestTx(root, limitsUpdateResult.success().manageLimitsRequestID,
                                                          ReviewRequestOpAction::APPROVE, "");
        }
    }
    SECTION("Create same request for second time")
    {
        limitsUpdateResult = limitsUpdateRequestHelper.applyCreateLimitsUpdateRequest(requestor, limitsUpdateRequest,
                             CreateManageLimitsRequestResultCode::MANAGE_LIMITS_REQUEST_REFERENCE_DUPLICATION);
    }
    SECTION("Approve and create same request for second time")
    {
        reviewLimitsUpdateHelper.applyReviewRequestTx(root, limitsUpdateResult.success().manageLimitsRequestID,
                                                      ReviewRequestOpAction::APPROVE, "");
        limitsUpdateResult = limitsUpdateRequestHelper.applyCreateLimitsUpdateRequest(requestor, limitsUpdateRequest,
                             CreateManageLimitsRequestResultCode::MANAGE_LIMITS_REQUEST_REFERENCE_DUPLICATION);
    }
}