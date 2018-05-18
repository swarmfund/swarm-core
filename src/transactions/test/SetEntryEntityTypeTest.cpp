// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the ISC License. See the COPYING file at the top-level directory of
// this distribution or at http://opensource.org/licenses/ISC

#include <ledger/EntityTypeHelper.h>
#include <transactions/test/test_helper/SetEntryEntityTypeTestHelper.h>
#include "main/Application.h"
#include "overlay/LoopbackPeer.h"
#include "util/make_unique.h"
#include "main/test.h"
#include "TxTests.h"
#include "ledger/LedgerDelta.h"
#include "ledger/FeeHelper.h"
#include "transactions/SetFeesOpFrame.h"
#include "crypto/SHA.h"
#include "test_helper/CreateAccountTestHelper.h"
#include "test_helper/ManageAssetTestHelper.h"
#include "test_helper/ManageAssetPairTestHelper.h"
#include "test_helper/SetFeesTestHelper.h"
#include "test/test_marshaler.h"
#include "ledger/AssetHelper.h"
#include "ledger/AccountHelper.h"

using namespace stellar;
using namespace stellar::txtest;

typedef std::unique_ptr<Application> appPtr;

TEST_CASE("Set entry entity type", "[tx][set_entity_type]") {
    Config const &cfg = getTestConfig(0, Config::TESTDB_POSTGRESQL);

    VirtualClock clock;
    Application::pointer appPtr = Application::create(clock, cfg);
    Application &app = *appPtr;
    app.start();
    TestManager::upgradeToCurrentLedgerVersion(app);

    Database &db = app.getDatabase();

    auto testManager = TestManager::make(app);

    LedgerDelta delta(app.getLedgerManager().getCurrentLedgerHeader(),
                      app.getDatabase());

    // set up world
    auto master = Account{getRoot(), Salt(1)};

    auto entityTypeHelper = EntityTypeHelper::Instance();

    CreateAccountTestHelper createAccountTestHelper(testManager);
    SetEntryEntityTypeTestHelper setEntityTypeEntryTestHelper(testManager);

    EntityType accountEntityType = EntityType::AccountType;

    SECTION("Invalid type") {
        auto entityTypeEntry = setEntityTypeEntryTestHelper.createEntityTypeEntry(EntityType(3), 7, "GENERAL");
        setEntityTypeEntryTestHelper.applySetEntityTypeTx(master, entityTypeEntry, false,
                                                          SetEntityTypeResultCode::INVALID_TYPE);
    }

    SECTION("Invalid name") {
        auto entityTypeEntry = setEntityTypeEntryTestHelper.createEntityTypeEntry(accountEntityType, 7533, "");
        setEntityTypeEntryTestHelper.applySetEntityTypeTx(master, entityTypeEntry, false,
                                                          SetEntityTypeResultCode::INVALID_NAME);
    }

    SECTION("Malformed") {
        EntityTypeEntry localEntityTypeEntry;
        localEntityTypeEntry.type = accountEntityType;
        setEntityTypeEntryTestHelper.applySetEntityTypeTx(master, localEntityTypeEntry, false,
                                                          SetEntityTypeResultCode::MALFORMED);
    }

    SECTION("Not_found") {
        auto entityTypeEntry = setEntityTypeEntryTestHelper.createEntityTypeEntry(accountEntityType, 7533, "GENERAL");
        setEntityTypeEntryTestHelper.applySetEntityTypeTx(master, entityTypeEntry, true,
                                                          SetEntityTypeResultCode::NOT_FOUND);
    }

    SECTION("Happy path") {
        auto entityTypeEntry = setEntityTypeEntryTestHelper.createEntityTypeEntry(accountEntityType , 123321,
                                                                                  "HappyPathTypeName");
        setEntityTypeEntryTestHelper.applySetEntityTypeTx(master, entityTypeEntry, false,
                                                          SetEntityTypeResultCode::SUCCESS);

        SECTION("Successful delete"){
            setEntityTypeEntryTestHelper.applySetEntityTypeTx(master, entityTypeEntry, true,
                                                              SetEntityTypeResultCode::SUCCESS);
        }
    }
}

