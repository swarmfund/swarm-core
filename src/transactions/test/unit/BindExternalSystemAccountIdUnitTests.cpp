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
#include "transactions/test/mocks/MockLedgerDelta.h"
#include "transactions/test/mocks/MockLedgerManager.h"
#include "transactions/test/mocks/MockStorageHelper.h"
#include "transactions/test/mocks/MockTransactionFrame.h"
#include "util/StatusManager.h"
#include "util/Timer.h"
#include "util/TmpDir.h"
#include "work/WorkManager.h"

using namespace stellar;

static int32 externalSystemType = 5;
static uint256 sourceAccountPublicKey = hexToBin256("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAABB");

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

    AccountFrame::pointer accountFrameFake = AccountFrame::makeAuthOnlyAccount(*operation.sourceAccount);

    ON_CALL(appMock, getDatabase()).WillByDefault(::testing::ReturnRef(dbMock));
    ON_CALL(storageHelperMock, getDatabase())
        .WillByDefault(::testing::ReturnRef(dbMock));
    ON_CALL(storageHelperMock, getLedgerDelta())
        .WillByDefault(::testing::ReturnRef(ledgerDeltaMock));

    ON_CALL(storageHelperMock, getExternalSystemAccountIDHelper())
        .WillByDefault(::testing::ReturnRef(externalSystemAccountIDHelperMock));
    ON_CALL(storageHelperMock, getExternalSystemAccountIDPoolEntryHelper())
        .WillByDefault(
            ::testing::ReturnRef(externalSystemAccountIDPoolEntryHelperMock));

    BindExternalSystemAccountIdOpFrame opFrame(operation, operationResult,
                                               transactionFrameMock);

    SECTION("Load account")
    {
        EXPECT_CALL(transactionFrameMock, loadAccount(&ledgerDeltaMock, ::testing::Ref(dbMock),
                                                      *operation.sourceAccount))
                .WillOnce(::testing::Return(accountFrameFake));
        REQUIRE(opFrame.loadAccount(&ledgerDeltaMock, dbMock));
    }

    /*SECTION("Apply, external system pool exists")
    {
        BindExternalSystemAccountIdOpFrame opFrame(operation, operationResult,
                                                   transactionFrameMock);

        // EXPECT_CALL( ... );
        opFrame.doApply(appMock, storageHelperMock, ledgerManagerMock);
    }*/

    /*SECTION("Checking validity of valid frame")
    {
        BindExternalSystemAccountIdOpFrame opFrame(operationMock,
    operationFrameMock, parentTxFrameMock);
        EXCEPT_TRUE(opFrame.doCheckValid(appMock));
    }

    SECTION("Checking validity of invalid frame")
    {
        BindExternalSystemAccountIdOpFrame opFrame(operationMock,
    operationFrameMock, parentTxFrameMock);
        EXCEPT_FALSE(opFrame.doCheckValid(appMock));
    }*/
}
