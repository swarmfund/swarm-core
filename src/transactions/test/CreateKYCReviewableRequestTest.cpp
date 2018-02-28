#include "test_helper/TxHelper.h"
#include "test_helper/CreateKYCReviewableRequestTestHelper.h"
#include "test/test_marshaler.h"
#include "main/test.h"
#include "ledger/AccountHelper.h"
#include "ledger/AccountKYCHelper.h"
#include "ledger/ReviewableRequestHelper.h"
#include "bucket/BucketApplicator.h"
#include "test_helper/CreateAccountTestHelper.h"
#include "test_helper/ReviewChangeKYCRequestHelper.h"
using namespace stellar;
using namespace stellar::txtest;

typedef std::unique_ptr<Application> appPtr;

TEST_CASE("create KYC request", "[tx][create_KYC_request]"){
    Config const &cfg = getTestConfig(0, Config::TESTDB_POSTGRESQL);

    VirtualClock clock;
    Application::pointer appPtr = Application::create(clock, cfg);
    Application &app = *appPtr;
    app.start();
    TestManager::upgradeToCurrentLedgerVersion(app);

    auto testManager = TestManager::make(app);

    auto updatedAccountID = SecretKey::random();

    auto updatedAccount = Account{updatedAccountID,Salt(1)};

    auto master = Account{getRoot(),Salt(1)};

    CreateAccountTestHelper accountTestHelper(testManager);

    accountTestHelper.applyCreateAccountTx(master,updatedAccountID.getPublicKey(),AccountType::GENERAL);



    CreateKYCRequestTestHelper testKYCRequestHelper(testManager);
    longstring kycData = "{}";
    uint32 kycLevel = 2;
    uint64 requestID = 0;
    SECTION("success") {
        SECTION("success, source master") {
           testKYCRequestHelper.applyCreateChangeKYCRequest(master, requestID, AccountType::COMMISSION,
                                                                       kycData,
                                                                       updatedAccountID.getPublicKey(), kycLevel,
                                                                       CreateKYCRequestResultCode::SUCCESS);
        }
        SECTION("source updated account") {
            auto tx =testKYCRequestHelper.applyCreateChangeKYCRequest(updatedAccount, requestID,
                                                                       AccountType::COMMISSION, kycData,
                                                                       updatedAccountID.getPublicKey(), kycLevel,
                                                                       CreateKYCRequestResultCode::SUCCESS);
            ReviewKYCRequestTestHelper reviewKYCRequestTestHelper(testManager);
            auto request = ReviewableRequestHelper::Instance()->loadRequest(tx.success().requestID,
                                                                            updatedAccountID.getPublicKey(),
                                                                            ReviewableRequestType::CHANGE_KYC,
                                                                            testManager->getDB());
            SECTION("success,approve request") {

                reviewKYCRequestTestHelper.applyReviewRequestTx(master, tx.success().requestID, request->getHash(),
                                                                ReviewableRequestType::CHANGE_KYC,
                                                                ReviewRequestOpAction::APPROVE, "",
                                                                ReviewRequestResultCode::SUCCESS);
            }
        }
    }
    SECTION("failed") {
        SECTION("set the same type") {
            kycLevel = 0;
            testKYCRequestHelper.applyCreateChangeKYCRequest(master, requestID, AccountType::GENERAL, kycData,
                                                                       updatedAccountID.getPublicKey(), kycLevel,
                                                                       CreateKYCRequestResultCode::SET_TYPE_THE_SAME);

        }
        SECTION("double creating, request exist") {
            testKYCRequestHelper.applyCreateChangeKYCRequest(updatedAccount, requestID,
                                                                       AccountType::COMMISSION, kycData,
                                                                       updatedAccountID.getPublicKey(), kycLevel,
                                                                       CreateKYCRequestResultCode::SUCCESS);

            testKYCRequestHelper.applyCreateChangeKYCRequest(master, requestID, AccountType::MASTER, kycData,
                                                                  updatedAccountID.getPublicKey(), kycLevel,
                                                                  CreateKYCRequestResultCode::REQUEST_EXIST);

        }
        SECTION("updated request not exist") {
            testKYCRequestHelper.applyCreateChangeKYCRequest(updatedAccount, 100, AccountType::COMMISSION,
                                                                       kycData,
                                                                       updatedAccountID.getPublicKey(), kycLevel,
                                                                       CreateKYCRequestResultCode::REQUEST_NOT_EXIST);

        }
    }
}