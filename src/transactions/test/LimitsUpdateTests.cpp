// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include <ledger/AccountHelper.h>
#include <ledger/FeeHelper.h>
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
#include "test_helper/SetLimitsTestHelper.h"
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
    auto setLimitsTestHelper = SetLimitsTestHelper(testManager);
    auto setOptionsTestHelper = SetOptionsTestHelper(testManager);

    // create requestor
    auto requestor = Account{ SecretKey::random(), Salt(0) };
    AccountID requestorID = requestor.key.getPublicKey();
    createAccountTestHelper.applyCreateAccountTx(root, requestorID,
                                                 AccountType::GENERAL);

    // create and apply initial limits for requestor
    Limits limits;
    limits.dailyOut = 100;
    limits.weeklyOut = 200;
    limits.monthlyOut = 300;
    limits.annualOut = 300;
    setLimitsTestHelper.applySetLimitsTx(root, &requestorID, nullptr, limits);

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
            reviewLimitsUpdateHelper.applyReviewRequestTx(root, limitsUpdateResult.success().limitsUpdateRequestID,
                                                          ReviewRequestOpAction::APPROVE, "");
        }
        SECTION("Reject")
        {
            reviewLimitsUpdateHelper.applyReviewRequestTx(root, limitsUpdateResult.success().limitsUpdateRequestID,
                                                          ReviewRequestOpAction::PERMANENT_REJECT, "Invalid document");
        }
        SECTION("Approve for account without limits")
        {
            auto accountWithoutLimits = Account {SecretKey::random(), Salt(0)};
            AccountID accountWithoutLimitsID = accountWithoutLimits.key.getPublicKey();
            createAccountTestHelper.applyCreateAccountTx(root, accountWithoutLimitsID,
                                                         AccountType::GENERAL);
            std::string documentData2 = "Some other document data";
            stellar::Hash documentHash2 = Hash(sha256(documentData2));

            auto limitsUpdateRequest2 = limitsUpdateRequestHelper.createLimitsUpdateRequest(documentHash2);
            auto limitsUpdateResult2 = limitsUpdateRequestHelper.applyCreateLimitsUpdateRequest(accountWithoutLimits,
                                                                                                limitsUpdateRequest2);

            reviewLimitsUpdateHelper.applyReviewRequestTx(root, limitsUpdateResult2.success().limitsUpdateRequestID,
                                                          ReviewRequestOpAction::APPROVE, "");
        }
    }
    SECTION("Create same request for second time")
    {
        auto limitsUpdateRequest2 = limitsUpdateRequestHelper.createLimitsUpdateRequest(documentHash);
        auto limitsUpdateResult2 = limitsUpdateRequestHelper.applyCreateLimitsUpdateRequest(requestor, limitsUpdateRequest,
                                                        SetOptionsResultCode::LIMITS_UPDATE_REQUEST_REFERENCE_DUPLICATION);
    }
}