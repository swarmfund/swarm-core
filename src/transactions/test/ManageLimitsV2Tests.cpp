#include <ledger/LimitsV2Helper.h>
#include "ledger/LedgerDeltaImpl.h"
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

	LedgerDeltaImpl delta(app.getLedgerManager().getCurrentLedgerHeader(), app.getDatabase());

	upgradeToCurrentLedgerVersion(app);

    // set up world
    auto root = Account{ getRoot(), Salt(0) };
    auto a1 = Account { getAccount("A"), Salt(0) };

    CreateAccountTestHelper createAccountTestHelper(testManager);
    auto txFrame = createAccountTestHelper.createCreateAccountTx(root, a1.key.getPublicKey(), AccountType::GENERAL);
    testManager->applyCheck(txFrame);

    ManageLimitsOp manageLimitsOp;
    manageLimitsOp.details.action(ManageLimitsAction::CREATE);
    AccountID accountID = a1.key.getPublicKey();
    AccountType accountType = AccountType::GENERAL;
    manageLimitsOp.details.limitsCreateDetails().accountID.activate() = accountID;
    manageLimitsOp.details.limitsCreateDetails().accountType.activate() = accountType;
    manageLimitsOp.details.limitsCreateDetails().assetCode = "USD";
    manageLimitsOp.details.limitsCreateDetails().statsOpType = StatsOpType::PAYMENT_OUT;
    manageLimitsOp.details.limitsCreateDetails().isConvertNeeded = false;
    manageLimitsOp.details.limitsCreateDetails().dailyOut = 100;
    manageLimitsOp.details.limitsCreateDetails().weeklyOut = 200;
    manageLimitsOp.details.limitsCreateDetails().monthlyOut = 300;
    manageLimitsOp.details.limitsCreateDetails().annualOut = 500;

	auto limitsV2Helper = LimitsV2Helper::Instance();

    ManageLimitsTestHelper manageLimitsTestHelper(testManager);

    SECTION("malformed")
    {
        manageLimitsTestHelper.applyManageLimitsTx(root, manageLimitsOp,
                                                   ManageLimitsResultCode::CANNOT_CREATE_FOR_ACC_ID_AND_ACC_TYPE);
        manageLimitsOp.details.limitsCreateDetails().annualOut = 0;
        manageLimitsOp.details.limitsCreateDetails().accountType = nullptr;
        manageLimitsTestHelper.applyManageLimitsTx(root, manageLimitsOp, ManageLimitsResultCode::INVALID_LIMITS);
    }
    SECTION("success accountID limits setting")
    {
        manageLimitsOp.details.limitsCreateDetails().accountType = nullptr;
        manageLimitsTestHelper.applyManageLimitsTx(root, manageLimitsOp);
        auto limitsAfter = limitsV2Helper->loadLimits(app.getDatabase(),
                manageLimitsOp.details.limitsCreateDetails().statsOpType,
                manageLimitsOp.details.limitsCreateDetails().assetCode,
                manageLimitsOp.details.limitsCreateDetails().accountID, nullptr,
                manageLimitsOp.details.limitsCreateDetails().isConvertNeeded, nullptr);

        REQUIRE(!!limitsAfter);
        REQUIRE(limitsAfter->getDailyOut() == manageLimitsOp.details.limitsCreateDetails().dailyOut);
        REQUIRE(limitsAfter->getWeeklyOut() == manageLimitsOp.details.limitsCreateDetails().weeklyOut);
        REQUIRE(limitsAfter->getMonthlyOut() == manageLimitsOp.details.limitsCreateDetails().monthlyOut);
        REQUIRE(limitsAfter->getAnnualOut() == manageLimitsOp.details.limitsCreateDetails().annualOut);

        SECTION("success update if already set")
        {
            manageLimitsOp.details.limitsCreateDetails().annualOut = INT64_MAX;
            manageLimitsTestHelper.applyManageLimitsTx(root, manageLimitsOp);
            limitsAfter = limitsV2Helper->loadLimits(app.getDatabase(),
                    manageLimitsOp.details.limitsCreateDetails().statsOpType,
                    manageLimitsOp.details.limitsCreateDetails().assetCode,
                    manageLimitsOp.details.limitsCreateDetails().accountID, nullptr,
                    manageLimitsOp.details.limitsCreateDetails().isConvertNeeded, nullptr);

            REQUIRE(!!limitsAfter);
            REQUIRE(limitsAfter->getAnnualOut() == manageLimitsOp.details.limitsCreateDetails().annualOut);

            SECTION("success remove")
            {
                manageLimitsOp.details.action(ManageLimitsAction::REMOVE);
                manageLimitsOp.details.id() = limitsAfter->getID();
                manageLimitsTestHelper.applyManageLimitsTx(root, manageLimitsOp);
                auto removedLimits = limitsV2Helper->loadLimits(manageLimitsOp.details.id(), app.getDatabase());

                REQUIRE(!removedLimits);

                SECTION("not found removed limits")
                {
                    manageLimitsTestHelper.applyManageLimitsTx(root, manageLimitsOp, ManageLimitsResultCode::NOT_FOUND);
                    removedLimits = limitsV2Helper->loadLimits(manageLimitsOp.details.id(), app.getDatabase());

                    REQUIRE(!removedLimits);
                }
            }
        }

    }

    SECTION("success accountType limits update")
    {
        manageLimitsOp.details.limitsCreateDetails().accountID = nullptr;
        auto limitsBefore = limitsV2Helper->loadLimits(app.getDatabase(),
                manageLimitsOp.details.limitsCreateDetails().statsOpType,
                manageLimitsOp.details.limitsCreateDetails().assetCode,
                nullptr, manageLimitsOp.details.limitsCreateDetails().accountType,
                manageLimitsOp.details.limitsCreateDetails().isConvertNeeded, nullptr);

        REQUIRE(!limitsBefore);

        manageLimitsTestHelper.applyManageLimitsTx(root, manageLimitsOp);
        auto limitsAfterFrame = limitsV2Helper->loadLimits(app.getDatabase(),
                manageLimitsOp.details.limitsCreateDetails().statsOpType,
                manageLimitsOp.details.limitsCreateDetails().assetCode,
                nullptr, manageLimitsOp.details.limitsCreateDetails().accountType,
                manageLimitsOp.details.limitsCreateDetails().isConvertNeeded, nullptr);

        REQUIRE(!!limitsAfterFrame);
        REQUIRE(limitsAfterFrame->getDailyOut() == manageLimitsOp.details.limitsCreateDetails().dailyOut);
        REQUIRE(limitsAfterFrame->getWeeklyOut() == manageLimitsOp.details.limitsCreateDetails().weeklyOut);
        REQUIRE(limitsAfterFrame->getMonthlyOut() == manageLimitsOp.details.limitsCreateDetails().monthlyOut);
        REQUIRE(limitsAfterFrame->getAnnualOut() == manageLimitsOp.details.limitsCreateDetails().annualOut);
    }
}