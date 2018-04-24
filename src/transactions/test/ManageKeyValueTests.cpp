#include <transactions/test/test_helper/TestManager.h>
#include <transactions/test/test_helper/ManageKeyValueTestHelper.h>
#include <transactions/ManageKeyValueOpFrame.h>
#include "overlay/LoopbackPeer.h"
#include "main/test.h"
#include "ledger/KeyValueHelper.h"
#include "test/test_marshaler.h"

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
    string256 key = SecretKey::random().getStrKeyPublic().substr(0,250);

    LedgerDelta delta(testManager->getLedgerManager().getCurrentLedgerHeader(),testManager->getDB());

    ManageKeyValueTestHelper testHelper(testManager);
    testHelper.setKey(key);

    auto keyValueHelper = KeyValueHelper::Instance();

    SECTION("Can`t delete before create"){
        testHelper.setResult(ManageKeyValueResultCode::NOT_FOUND);
        testHelper.doAply(app, delta, testManager->getLedgerManager(), ManageKVAction::DELETE, false);
    }

    SECTION("Can`t load before create"){
        auto kvFrame = keyValueHelper->loadKeyValue(key,testManager->getDB());
        REQUIRE(!kvFrame);
    }

    SECTION("Can create new") {
        testHelper.setResult(ManageKeyValueResultCode::SUCCESS);
        testHelper.doAply(app, delta, testManager->getLedgerManager(), ManageKVAction::PUT);

        SECTION("Can load after create") {
            auto kvFrame = keyValueHelper->loadKeyValue(key, testManager->getDB());
            REQUIRE(!!kvFrame);
        }

        SECTION("Can update after create") {
            auto kvFrame = keyValueHelper->loadKeyValue(key, testManager->getDB());
            REQUIRE(!!kvFrame);
            testHelper.doAply(app, delta, testManager->getLedgerManager(), ManageKVAction::PUT);
        }

        SECTION("Can delete after create") {
            auto kvFrame = keyValueHelper->loadKeyValue(key, testManager->getDB());
            REQUIRE(!!kvFrame);
            testHelper.doAply(app, delta, testManager->getLedgerManager(), ManageKVAction::DELETE);


            SECTION("Can`t load after delete") {
                auto kvFrame = keyValueHelper->loadKeyValue(key, testManager->getDB());
                REQUIRE(!kvFrame);
            }

            SECTION("Can`t delete after delete") {
                testHelper.setResult(ManageKeyValueResultCode::NOT_FOUND);
                testHelper.doAply(app, delta, testManager->getLedgerManager(), ManageKVAction::DELETE, false);
            }

            SECTION("Can add again after delete") {
                testHelper.setResult(ManageKeyValueResultCode::SUCCESS);
                testHelper.doAply(app, delta, testManager->getLedgerManager(), ManageKVAction::PUT);
            }
        }
    }

}