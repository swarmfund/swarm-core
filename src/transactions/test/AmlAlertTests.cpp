#include "main/Application.h"
#include "test_helper/TestManager.h"
#include "test/test_marshaler.h"
#include "TxTests.h"
#include "main/test.h"
#include "test_helper/ManageAMLAlertTestHelper.h"
#include "test_helper/IssuanceRequestHelper.h"
#include "test_helper/ManageAssetTestHelper.h"
#include "ledger/BalanceHelperLegacy.h"
#include "test_helper/CreateAccountTestHelper.h"
#include "test_helper/ReviewAMLAlertRequestHelper.h"

using namespace stellar;
using namespace stellar::txtest;

typedef std::unique_ptr<Application> appPtr;


TEST_CASE("Aml alert", "[tx][aml_alert]")
{
    Config const& cfg = getTestConfig(0, Config::TESTDB_POSTGRESQL);
    VirtualClock clock;
    Application::pointer appPtr = Application::create(clock, cfg);
    Application& app = *appPtr;
    app.start();
    auto testManager = TestManager::make(app);
    TestManager::upgradeToCurrentLedgerVersion(app);

    Database& db = testManager->getDB();

    auto root = Account{ getRoot(), Salt(0) };
    auto amlAlertHelper = ManageAMLAlertTestHelper(testManager);
    SECTION("Empty reason is not allowed")
    {
        BalanceID balanceID;
        amlAlertHelper.applyCreateAmlAlert(root, balanceID, 0, "", "", CreateAMLAlertRequestResultCode::INVALID_REASON);
    }
    SECTION("Zero amount is not allowed")
    {
        BalanceID balanceID;
        amlAlertHelper.applyCreateAmlAlert(root, balanceID, 0, "Inalid", "", CreateAMLAlertRequestResultCode::INVALID_AMOUNT);
    }
    SECTION("Balance does not exists")
    {
        BalanceID balanceID;
        amlAlertHelper.applyCreateAmlAlert(root, balanceID, 10, "Inalid", "", CreateAMLAlertRequestResultCode::BALANCE_NOT_EXIST);
    }

    auto reference = "Random reference";
    SECTION("Given valid account with balance")
    {
        auto issuanceHelper = IssuanceRequestHelper(testManager);
        const AssetCode asset = "USD";
        const uint64_t preIssuedAmount = 10000 * ONE;
        issuanceHelper.createAssetWithPreIssuedAmount(root, asset, preIssuedAmount, root);
        ManageAssetTestHelper(testManager).updateAsset(root, asset, root, static_cast<uint32_t>(AssetPolicy::BASE_ASSET) | static_cast<uint32_t>(AssetPolicy::WITHDRAWABLE));

        auto account = SecretKey::random();
        CreateAccountTestHelper(testManager).applyCreateAccountTx(root, account.getPublicKey(), AccountType::GENERAL);
        auto balance = BalanceHelperLegacy::Instance()->loadBalance(account.getPublicKey(), asset, testManager->getDB(), nullptr);
        REQUIRE(!!balance);
        uint32_t allTasks = 0;
        issuanceHelper.applyCreateIssuanceRequest(root, asset, preIssuedAmount, balance->getBalanceID(),
                                                  "RANDOM ISSUANCE REFERENCE", &allTasks);
        SECTION("Insuficient balance")
        {
            amlAlertHelper.applyCreateAmlAlert(root, balance->getBalanceID(), preIssuedAmount + 1, "Inalid", reference, CreateAMLAlertRequestResultCode::UNDERFUNDED);
        }
        SECTION("Given valid aml alert request")
        {
            const auto requestID = amlAlertHelper.applyCreateAmlAlert(root, balance->getBalanceID(), preIssuedAmount/2, "Inalid", reference,
                CreateAMLAlertRequestResultCode::SUCCESS).success().requestID;
            SECTION("Reference duplication")
            {
                amlAlertHelper.applyCreateAmlAlert(root, balance->getBalanceID(), preIssuedAmount / 2, "Inalid", reference, CreateAMLAlertRequestResultCode::REFERENCE_DUPLICATION);
            }
            auto amlReviewHelper = ReviewAmlAlertHelper(testManager);
            SECTION("Approve")
            {
                amlReviewHelper.applyReviewRequestTx(root, requestID, ReviewRequestOpAction::APPROVE, "");
                SECTION("Reference duplication")
                {
                    amlAlertHelper.applyCreateAmlAlert(root, balance->getBalanceID(), preIssuedAmount / 2, "Inalid", reference, CreateAMLAlertRequestResultCode::REFERENCE_DUPLICATION);
                }
            }
            SECTION("Reject is not allowed")
            {
                amlReviewHelper.applyReviewRequestTx(root, requestID, ReviewRequestOpAction::REJECT, "Everthing seems to be ok", ReviewRequestResultCode::REJECT_NOT_ALLOWED);
            }
            SECTION("Permanent Reject")
            {
                amlReviewHelper.applyReviewRequestTx(root, requestID, ReviewRequestOpAction::PERMANENT_REJECT, "Everthing seems to be ok");
                SECTION("Reference duplication")
                {
                    amlAlertHelper.applyCreateAmlAlert(root, balance->getBalanceID(), preIssuedAmount / 2, "Inalid", reference, CreateAMLAlertRequestResultCode::REFERENCE_DUPLICATION);
                }
            }
        }
    }
}