#include <transactions/test/test_helper/TestManager.h>
#include <transactions/test/test_helper/ManageKeyValueTestHelper.h>
#include <transactions/ManageKeyValueOpFrame.h>
#include "overlay/LoopbackPeer.h"
#include "main/test.h"
#include "ledger/KeyValueHelperLegacy.h"
#include "test/test_marshaler.h"
#include "TxTests.h"

using namespace stellar;
using namespace stellar::txtest;

typedef std::unique_ptr<Application> appPtr;

TEST_CASE("manage KeyValue", "[tx][manage_key_value]") {
    Config const &cfg = getTestConfig(0, Config::TESTDB_POSTGRESQL);

    VirtualClock clock;
    Application::pointer appPtr = Application::create(clock, cfg);
    Application &app = *appPtr;
    app.start();
    TestManager::upgradeToCurrentLedgerVersion(app);

    auto testManager = TestManager::make(app);
    longstring key = SecretKey::random().getStrKeyPublic();

    ManageKeyValueTestHelper testHelper(testManager);
    testHelper.setKey(key);
    testHelper.setUi32Value(30);

    auto keyValueHelper = KeyValueHelperLegacy::Instance();

    SECTION("Can`t delete before create"){
        testHelper.setResult(ManageKeyValueResultCode::NOT_FOUND);
        testHelper.doApply(app, ManageKVAction::REMOVE, false);
    }

    SECTION("Can`t load before create"){
        auto kvFrame = keyValueHelper->loadKeyValue(key,testManager->getDB());
        REQUIRE(!kvFrame);
    }

    SECTION("Invalid type"){
        testHelper.setResult(ManageKeyValueResultCode::INVALID_TYPE);
        auto kvFrame = keyValueHelper->loadKeyValue(key,testManager->getDB());
        REQUIRE(!kvFrame);
        longstring localKey = ManageKeyValueOpFrame::makeKYCRuleKey(AccountType::MASTER, 3, AccountType::GENERAL, 5);
        testHelper.setKey(localKey);
        testHelper.setValue("smth");
        testHelper.doApply(app, ManageKVAction::PUT, false, KeyValueEntryType::STRING);
    }

    testHelper.setKey(key);

    SECTION("Can create new") {
        testHelper.setResult(ManageKeyValueResultCode::SUCCESS);
        testHelper.doApply(app, ManageKVAction::PUT, true);

        SECTION("Can load after create") {
            auto kvFrame = keyValueHelper->loadKeyValue(key, testManager->getDB());
            REQUIRE(!!kvFrame);
        }

        SECTION("Can update after create") {
            auto kvFrame = keyValueHelper->loadKeyValue(key, testManager->getDB());
            REQUIRE(!!kvFrame);
            testHelper.setUi32Value(40);
            testHelper.doApply(app, ManageKVAction::PUT, true);
        }

        SECTION("Can delete after create") {
            auto kvFrame = keyValueHelper->loadKeyValue(key, testManager->getDB());
            REQUIRE(!!kvFrame);
            testHelper.doApply(app, ManageKVAction::REMOVE, true);


            SECTION("Can`t load after delete") {
                auto kvFrame = keyValueHelper->loadKeyValue(key, testManager->getDB());
                REQUIRE(!kvFrame);
            }

            SECTION("Can`t delete after delete") {
                testHelper.setResult(ManageKeyValueResultCode::NOT_FOUND);
                testHelper.doApply(app, ManageKVAction::REMOVE, false);
            }

            SECTION("Can add again after delete") {
                testHelper.setResult(ManageKeyValueResultCode::SUCCESS);
                testHelper.doApply(app, ManageKVAction::PUT, true);
            }
        }
    }

}