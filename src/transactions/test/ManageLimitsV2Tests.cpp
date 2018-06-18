#include <ledger/LimitsV2Helper.h>
#include <transactions/test/test_helper/ManageLimitsTestHelper.h>
#include "main/Application.h"
#include "main/Config.h"
#include "util/Timer.h"
#include "overlay/LoopbackPeer.h"
#include "util/make_unique.h"
#include "main/test.h"
#include "TxTests.h"
#include "test/test_marshaler.h"
#include "test_helper/CreateAccountTestHelper.h"

using namespace stellar;
using namespace stellar::txtest;

typedef std::unique_ptr<Application> appPtr;

// Try setting each option to make sure it works
// try setting all at once
// try setting high threshold ones without the correct sigs
// make sure it doesn't allow us to add signers when we don't have the
// minbalance
TEST_CASE("manage limits", "[tx][manage_limits]")
{
    using xdr::operator==;

    Config const& cfg = getTestConfig(0, Config::TESTDB_POSTGRESQL);

    VirtualClock clock;
    Application::pointer appPtr = Application::create(clock, cfg);
    Application& app = *appPtr;
    app.start();

    auto testManager = TestManager::make(app);

	LedgerDelta delta(app.getLedgerManager().getCurrentLedgerHeader(), app.getDatabase());

	upgradeToCurrentLedgerVersion(app);

    // set up world
    auto root = Account{ getRoot(), Salt(0) };
    auto a1 = Account { getAccount("A"), Salt(0) };

    CreateAccountTestHelper createAccountTestHelper(testManager);
    auto txFrame = createAccountTestHelper.createCreateAccountTx(root, a1.key.getPublicKey(), AccountType::GENERAL);
    testManager->applyCheck(txFrame);

    ManageLimitsOp manageLimitsOp;
    AccountID accountID = a1.key.getPublicKey();
    AccountType accountType = AccountType::GENERAL;
    manageLimitsOp.accountID.activate() = accountID;
    manageLimitsOp.accountType.activate() = accountType;
    manageLimitsOp.assetCode = "USD";
    manageLimitsOp.statsOpType = StatsOpType::PAYMENT_OUT;
    manageLimitsOp.isConvertNeeded = false;
    manageLimitsOp.isDelete = false;
    manageLimitsOp.dailyOut = 100;
    manageLimitsOp.weeklyOut = 200;
    manageLimitsOp.monthlyOut = 300;
    manageLimitsOp.annualOut = 500;

	auto limitsV2Helper = LimitsV2Helper::Instance();

    ManageLimitsTestHelper manageLimitsTestHelper(testManager);

    SECTION("malformed")
    {
        manageLimitsTestHelper.applyManageLimitsTx(root, manageLimitsOp, ManageLimitsResultCode::MALFORMED);
        manageLimitsOp.annualOut = 0;
        manageLimitsOp.accountType = nullptr;
        manageLimitsTestHelper.applyManageLimitsTx(root, manageLimitsOp, ManageLimitsResultCode::MALFORMED);
    }
    SECTION("success accountID limits setting")
    {
        manageLimitsOp.accountType = nullptr;
        manageLimitsTestHelper.applyManageLimitsTx(root, manageLimitsOp);
        auto limitsAfter = limitsV2Helper->loadLimits(app.getDatabase(), manageLimitsOp.statsOpType,
                                                      manageLimitsOp.assetCode, manageLimitsOp.accountID, nullptr,
                                                      manageLimitsOp.isConvertNeeded, nullptr);
        REQUIRE(!!limitsAfter);
        REQUIRE(limitsAfter->getDailyOut() == manageLimitsOp.dailyOut);
        REQUIRE(limitsAfter->getWeeklyOut() == manageLimitsOp.weeklyOut);
        REQUIRE(limitsAfter->getMonthlyOut() == manageLimitsOp.monthlyOut);
        REQUIRE(limitsAfter->getAnnualOut() == manageLimitsOp.annualOut);

        SECTION("success update if already set")
        {
            manageLimitsOp.annualOut = INT64_MAX;
            manageLimitsTestHelper.applyManageLimitsTx(root, manageLimitsOp);
            auto limitsAfter = limitsV2Helper->loadLimits(app.getDatabase(), manageLimitsOp.statsOpType,
                                                          manageLimitsOp.assetCode, manageLimitsOp.accountID, nullptr,
                                                          manageLimitsOp.isConvertNeeded, nullptr);
            REQUIRE(!!limitsAfter);
            REQUIRE(limitsAfter->getAnnualOut() == manageLimitsOp.annualOut);

            SECTION("success delete")
            {
                manageLimitsOp.isDelete = true;
                manageLimitsTestHelper.applyManageLimitsTx(root, manageLimitsOp);
                auto deletedLimits = limitsV2Helper->loadLimits(app.getDatabase(), manageLimitsOp.statsOpType,
                                                              manageLimitsOp.assetCode, manageLimitsOp.accountID, nullptr,
                                                              manageLimitsOp.isConvertNeeded, nullptr);
                REQUIRE(!deletedLimits);
            }
        }

    }

    SECTION("success accountType limits update")
    {
        manageLimitsOp.accountType.activate() = accountType;
        manageLimitsOp.accountID = nullptr;
        auto limitsBefore = limitsV2Helper->loadLimits(app.getDatabase(), manageLimitsOp.statsOpType,
                                                      manageLimitsOp.assetCode, nullptr, manageLimitsOp.accountType,
                                                      manageLimitsOp.isConvertNeeded, nullptr);
        REQUIRE(!limitsBefore);

        manageLimitsTestHelper.applyManageLimitsTx(root, manageLimitsOp);
        auto limitsAfterFrame = limitsV2Helper->loadLimits(app.getDatabase(), manageLimitsOp.statsOpType,
                                                          manageLimitsOp.assetCode, nullptr, manageLimitsOp.accountType,
                                                          manageLimitsOp.isConvertNeeded, nullptr);
        REQUIRE(!!limitsAfterFrame);
        REQUIRE(limitsAfterFrame->getDailyOut() == manageLimitsOp.dailyOut);
        REQUIRE(limitsAfterFrame->getWeeklyOut() == manageLimitsOp.weeklyOut);
        REQUIRE(limitsAfterFrame->getMonthlyOut() == manageLimitsOp.monthlyOut);
        REQUIRE(limitsAfterFrame->getAnnualOut() == manageLimitsOp.annualOut);

        SECTION("it works for created accounts")
        {
            auto a2 = Account { SecretKey::random(), Salt(0)};
            auto receiver = Account { SecretKey::random(), Salt(0)};
            createAccountTestHelper.applyCreateAccountTx(root, a2.key.getPublicKey(), accountType);
            createAccountTestHelper.applyCreateAccountTx(root, receiver.key.getPublicKey(), accountType);
        }
    }
}