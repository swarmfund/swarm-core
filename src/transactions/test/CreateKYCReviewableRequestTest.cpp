#include <transactions/test/test_helper/ManageKeyValueTestHelper.h>
#include <arpa/nameser_compat.h>
#include "test_helper/TxHelper.h"
#include "test_helper/CreateKYCReviewableRequestTestHelper.h"
#include "test/test_marshaler.h"
#include "main/test.h"
#include "ledger/AccountHelper.h"
#include "ledger/AccountKYCHelper.h"
#include "ledger/ReviewableRequestHelper.h"
#include "bucket/BucketApplicator.h"
#include "test_helper/CreateAccountTestHelper.h"
#include "transactions/test/test_helper/ReviewUpdateKYCRequestHelper.h"

using namespace stellar;
using namespace stellar::txtest;

typedef std::unique_ptr<Application> appPtr;

TEST_CASE("create KYC request", "[tx][create_KYC_request]") {
    Config cfg = getTestConfig(0, Config::TESTDB_POSTGRESQL);
    cfg.LEDGER_PROTOCOL_VERSION = 13;

    VirtualClock clock;
    Application::pointer appPtr = Application::create(clock, cfg);
    Application &app = *appPtr;
    app.start();
    TestManager::upgradeToCurrentLedgerVersion(app);

    auto testManager = TestManager::make(app);

    auto updatedAccountID = SecretKey::random();

    auto updatedAccount = Account{updatedAccountID, Salt(1)};

    auto master = Account{getRoot(), Salt(1)};

    CreateAccountTestHelper accountTestHelper(testManager);

    accountTestHelper.applyCreateAccountTx(master, updatedAccountID.getPublicKey(), AccountType::GENERAL);


    CreateKYCRequestTestHelper testKYCRequestHelper(testManager);
    ManageKeyValueTestHelper manageKVHelper(testManager);

    longstring kycData = "{}";
    uint32 kycLevel = 2;
    uint64 requestID = 0;
    uint32 tasks = 0;
    ReviewKYCRequestTestHelper reviewKYCRequestTestHelper(testManager);

    LedgerDelta delta(testManager->getLedgerManager().getCurrentLedgerHeader(),testManager->getDB());
    auto account = AccountHelper::Instance()->loadAccount(updatedAccountID.getPublicKey(),
                                                          testManager->getDB());
    if(account)
    {
        account->getAccount().ext.v(LedgerVersion::USE_KYC_LEVEL);
        account->  setKYCLevel(0);
    }

    AccountHelper::Instance()->storeChange(delta,testManager->getDB(),account->mEntry);

    longstring key = ManageKeyValueOpFrame::makeKYCRuleKey(account->getAccount().accountType, account->getKYCLevel(),
                                                           AccountType::GENERAL, kycLevel);

    SECTION("success") {
        //add record of KV to DB
        if (testManager->getLedgerManager().shouldUse(LedgerVersion::KYC_RULES)) {
            manageKVHelper.setKey(key)->setValue(tasks);
            manageKVHelper.doAply(app, ManageKVAction::PUT, true);
        }

        SECTION("source master, create and approve") {

            auto createUpdateKYCRequestResult = testKYCRequestHelper.applyCreateUpdateKYCRequest(master, 0,
                                                                                                 updatedAccountID.getPublicKey(),
                                                                                                 AccountType::GENERAL,
                                                                                                 kycData, kycLevel,
                                                                                                 nullptr);

            requestID = createUpdateKYCRequestResult.success().requestID;
            auto request = ReviewableRequestHelper::Instance()->loadRequest(requestID, updatedAccountID.getPublicKey(),
                                                                            ReviewableRequestType::UPDATE_KYC,
                                                                            testManager->getDB());

            reviewKYCRequestTestHelper.applyReviewRequestTx(master, requestID, ReviewRequestOpAction::APPROVE, "");
        }
        SECTION("source master, autoapprove") {
            auto createUpdateKYCRequestResult = testKYCRequestHelper.applyCreateUpdateKYCRequest(master, 0,
                                                                                                 updatedAccountID.getPublicKey(),
                                                                                                 AccountType::GENERAL,
                                                                                                 kycData, kycLevel,
                                                                                                 &tasks);
        }
        SECTION("source updatedAccount, create -> reject -> update -> approve") {
            auto createUpdateKYCRequestResult = testKYCRequestHelper.applyCreateUpdateKYCRequest(updatedAccount, 0,
                                                                                                 updatedAccountID.getPublicKey(),
                                                                                                 AccountType::GENERAL,
                                                                                                 kycData, kycLevel,
                                                                                                 nullptr);

            requestID = createUpdateKYCRequestResult.success().requestID;
            auto request = ReviewableRequestHelper::Instance()->loadRequest(requestID, updatedAccountID.getPublicKey(),
                                                                            ReviewableRequestType::UPDATE_KYC,
                                                                            testManager->getDB());

            reviewKYCRequestTestHelper.applyReviewRequestTx(master, requestID, ReviewRequestOpAction::REJECT,
                                                            "Not enough docs for third kyc level");

            reviewKYCRequestTestHelper.applyReviewRequestTx(master, requestID, ReviewRequestOpAction::REJECT,
                                                            "One more reject, just for fun");

            auto changeUpdateKYCRequestResult = testKYCRequestHelper.applyCreateUpdateKYCRequest(updatedAccount,
                                                                                                 requestID,
                                                                                                 updatedAccountID.getPublicKey(),
                                                                                                 AccountType::GENERAL,
                                                                                                 kycData, kycLevel,
                                                                                                 nullptr);

            reviewKYCRequestTestHelper.applyReviewRequestTx(master, requestID, ReviewRequestOpAction::APPROVE, "");
        }
        SECTION("set the same type") {
            kycLevel = 0;

            key = ManageKeyValueOpFrame::makeKYCRuleKey(account->getAccount().accountType,account->getAccount().ext.kycLevel(),
                        AccountType::GENERAL,kycLevel);
            manageKVHelper.setKey(key)->doAply(app,ManageKVAction::PUT,true);
            
            testKYCRequestHelper.applyCreateUpdateKYCRequest(master, 0, updatedAccountID.getPublicKey(),
                                                             AccountType::GENERAL, kycData, kycLevel, nullptr);
        }
    }
    SECTION("failed") {
        SECTION("kyc rule not found") {
            if (testManager->getLedgerManager().shouldUse(LedgerVersion::KYC_RULES)) {
                testKYCRequestHelper.applyCreateUpdateKYCRequest(updatedAccount, 0, updatedAccountID.getPublicKey(),
                                                                 AccountType::GENERAL, kycData, kycLevel, nullptr,
                                                                 CreateUpdateKYCRequestResultCode::KYC_RULE_NOT_FOUND);
            }
        }

        if (testManager->getLedgerManager().shouldUse(LedgerVersion::KYC_RULES)) {
            manageKVHelper.setKey(key)->setValue(tasks);
            manageKVHelper.doAply(app, ManageKVAction::PUT, true);
        }

        SECTION("double creating, request exists") {
            testKYCRequestHelper.applyCreateUpdateKYCRequest(updatedAccount, 0, updatedAccountID.getPublicKey(),
                                                             AccountType::GENERAL, kycData, kycLevel, nullptr,
                                                             CreateUpdateKYCRequestResultCode::SUCCESS);

            testKYCRequestHelper.applyCreateUpdateKYCRequest(updatedAccount, 0, updatedAccountID.getPublicKey(),
                                                             AccountType::GENERAL, kycData, kycLevel, nullptr,
                                                             CreateUpdateKYCRequestResultCode::REQUEST_ALREADY_EXISTS);

        }
        SECTION("updated request does not exist") {
            testKYCRequestHelper.applyCreateUpdateKYCRequest(updatedAccount, 100, updatedAccountID.getPublicKey(),
                                                             AccountType::GENERAL, kycData, kycLevel, nullptr,
                                                             CreateUpdateKYCRequestResultCode::REQUEST_DOES_NOT_EXIST);

        }
        SECTION("update pending is not allowed for user") {
            auto createUpdateKYCRequestResult = testKYCRequestHelper.applyCreateUpdateKYCRequest(updatedAccount, 0,
                                                                                                 updatedAccountID.getPublicKey(),
                                                                                                 AccountType::GENERAL,
                                                                                                 kycData, kycLevel,
                                                                                                 nullptr,
                                                                                                 CreateUpdateKYCRequestResultCode::SUCCESS);
            requestID = createUpdateKYCRequestResult.success().requestID;
            testKYCRequestHelper.applyCreateUpdateKYCRequest(updatedAccount, requestID, updatedAccountID.getPublicKey(),
                                                             AccountType::GENERAL, kycData, kycLevel, nullptr,
                                                             CreateUpdateKYCRequestResultCode::PENDING_REQUEST_UPDATE_NOT_ALLOWED);
        }
        SECTION("source master, create and update pending") {
            tasks = 3;
            auto createUpdateKYCRequestResult = testKYCRequestHelper.applyCreateUpdateKYCRequest(master, 0,
                                                                                                 updatedAccountID.getPublicKey(),
                                                                                                 AccountType::GENERAL,
                                                                                                 kycData, kycLevel,
                                                                                                 &tasks);
            manageKVHelper.setValue(tasks);
            manageKVHelper.doAply(app, ManageKVAction::PUT, true);

            requestID = createUpdateKYCRequestResult.success().requestID;
            uint32 newTasks = 1;

            manageKVHelper.setValue(newTasks);
            manageKVHelper.doAply(app, ManageKVAction::PUT, true);

            testKYCRequestHelper.applyCreateUpdateKYCRequest(master, requestID, updatedAccountID.getPublicKey(),
                                                             AccountType::GENERAL, kycData, kycLevel, &newTasks,
                                                             CreateUpdateKYCRequestResultCode::NOT_ALLOWED_TO_UPDATE_REQUEST);
        }
    }
}