#include "bucket/BucketManager.h"
#include "herder/Herder.h"
#include "invariant/Invariants.h"
#include "ledger/LedgerHeaderFrame.h"
#include "main/CommandHandler.h"
#include "main/PersistentState.h"
#include "medida/meter.h"
#include "medida/metrics_registry.h"
#include "overlay/BanManager.h"
#include "overlay/OverlayManager.h"
#include "process/ProcessManager.h"
#include "simulation/LoadGenerator.h"
#include "test/test_marshaler.h"
#include "transactions/BindExternalSystemAccountIdOpFrame.h"
#include "transactions/test/mocks/MockApplication.h"
#include "transactions/test/mocks/MockDatabase.h"
#include "transactions/test/mocks/MockExternalSystemAccountIDHelper.h"
#include "transactions/test/mocks/MockExternalSystemAccountIDPoolEntryHelper.h"
#include "transactions/test/mocks/MockKeyValueHelper.h"
#include "transactions/test/mocks/MockBalanceHelper.h"
#include "transactions/test/mocks/MockAssetHelper.h"
#include "transactions/test/mocks/MockLedgerDelta.h"
#include "transactions/test/mocks/MockLedgerManager.h"
#include "transactions/test/mocks/MockSignatureValidator.h"
#include "transactions/test/mocks/MockStorageHelper.h"
#include "transactions/test/mocks/MockTransactionFrame.h"
#include "util/StatusManager.h"
#include "util/Timer.h"
#include "util/TmpDir.h"
#include "work/WorkManager.h"

using namespace stellar;
using namespace testing;

static int32 externalSystemType = 5;
static uint256 sourceAccountPublicKey = hexToBin256(
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAABB");

TEST_CASE("bind external system account_id - unit test",
          "[tx][bind_external_system_account_id_unit_test]")
{
    MockApplication appMock;
    MockLedgerManager ledgerManagerMock;
    MockTransactionFrame transactionFrameMock;
    MockLedgerDelta ledgerDeltaMock;
    MockDatabase dbMock;
    MockStorageHelper storageHelperMock;
    MockKeyValueHelper keyValueHelperMock;
    MockExternalSystemAccountIDHelper externalSystemAccountIDHelperMock;
    MockExternalSystemAccountIDPoolEntryHelper
        externalSystemAccountIDPoolEntryHelperMock;
    std::shared_ptr<MockSignatureValidator> signatureValidatorMock =
        std::make_shared<MockSignatureValidator>();

    BindExternalSystemAccountIdOp op;
    op.externalSystemType = externalSystemType;
    Operation operation;
    operation.body = Operation::_body_t(
        stellar::OperationType::BIND_EXTERNAL_SYSTEM_ACCOUNT_ID);
    operation.body.bindExternalSystemAccountIdOp() = op;
    operation.sourceAccount =
        xdr::pointer<AccountID>(new AccountID(CryptoKeyType::KEY_TYPE_ED25519));
    operation.sourceAccount->ed25519() = sourceAccountPublicKey;
    OperationResult operationResult;

    AccountFrame::pointer accountFrameFake =
        AccountFrame::makeAuthOnlyAccount(*operation.sourceAccount);
    LedgerHeader ledgerHeaderFake;

    Database::EntryCache cacheFake(4096);

    ON_CALL(appMock, getDatabase()).WillByDefault(ReturnRef(dbMock));
    ON_CALL(appMock, getLedgerManager())
        .WillByDefault(ReturnRef(ledgerManagerMock));
    ON_CALL(ledgerManagerMock, getCurrentLedgerHeader())
        .WillByDefault(ReturnRef(ledgerHeaderFake));
    ON_CALL(storageHelperMock, getDatabase()).WillByDefault(ReturnRef(dbMock));
    ON_CALL(storageHelperMock, getLedgerDelta())
        .WillByDefault(Return(&ledgerDeltaMock));
    ON_CALL(transactionFrameMock, getSignatureValidator())
        .WillByDefault(Return(signatureValidatorMock));
    ON_CALL(*signatureValidatorMock,
            check(Ref(appMock), Ref(dbMock), Ref(*accountFrameFake), _))
        .WillByDefault(Return(SignatureValidator::Result::SUCCESS));
    ON_CALL(dbMock, getEntryCache()).WillByDefault(ReturnRef(cacheFake));

    ON_CALL(storageHelperMock, getKeyValueHelper())
        .WillByDefault(ReturnRef(keyValueHelperMock));
    ON_CALL(storageHelperMock, getExternalSystemAccountIDHelper())
        .WillByDefault(ReturnRef(externalSystemAccountIDHelperMock));
    ON_CALL(storageHelperMock, getExternalSystemAccountIDPoolEntryHelper())
        .WillByDefault(ReturnRef(externalSystemAccountIDPoolEntryHelperMock));

    BindExternalSystemAccountIdOpFrame opFrame(operation, operationResult,
                                               transactionFrameMock);
    SECTION("Check validity")
    {
        EXPECT_CALL(transactionFrameMock,
                    loadAccount(&ledgerDeltaMock, Ref(dbMock),
                                *operation.sourceAccount))
            .WillOnce(Return(accountFrameFake));
        REQUIRE(opFrame.checkValid(appMock, &ledgerDeltaMock));

        SECTION("Apply, no pool entry to bind")
        {
            EXPECT_CALL(externalSystemAccountIDPoolEntryHelperMock,
                        load(op.externalSystemType, *operation.sourceAccount))
                .WillOnce(Return(nullptr));
            EXPECT_CALL(externalSystemAccountIDPoolEntryHelperMock,
                        loadAvailablePoolEntry(Ref(ledgerManagerMock),
                                               op.externalSystemType))
                .WillOnce(Return(nullptr));
            REQUIRE_FALSE(
                opFrame.doApply(appMock, storageHelperMock, ledgerManagerMock));
            REQUIRE(opFrame.getResult()
                        .tr()
                        .bindExternalSystemAccountIdResult()
                        .code() ==
                    BindExternalSystemAccountIdResultCode::NO_AVAILABLE_ID);
        }
    }
}
