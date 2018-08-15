#include "bucket/BucketManager.h"
#include "herder/Herder.h"
#include "invariant/Invariants.h"
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
#include "transactions/test/mocks/MockLedgerDelta.h"
#include "transactions/test/mocks/MockLedgerManager.h"
#include "transactions/test/mocks/MockStorageHelper.h"
#include "transactions/test/mocks/MockTransactionFrame.h"
#include "transactions/test/mocks/MockKeyValueHelper.h"
#include "transactions/test/mocks/MockExternalSystemAccountIDHelper.h"
#include "transactions/test/mocks/MockExternalSystemAccountIDPoolEntryHelper.h"
#include "util/StatusManager.h"
#include "util/Timer.h"
#include "util/TmpDir.h"
#include "work/WorkManager.h"
#include "ledger/LedgerHeaderFrame.h"

using namespace stellar;
typedef std::unique_ptr<Application> appPtr;

TEST_CASE("bind external system account_id - unit test",
          "[tx][bind_external_system_account_id_unit_test]")
{
    MockApplication appMock;
    MockLedgerManager ledgerManagerMock;
    MockTransactionFrame transactionFrameMock;
    MockLedgerDelta ledgerDeltaMock;
    MockDatabase dbMock;
    MockStorageHelper storageHelper;
    MockKeyValueHelper keyValueHelperMock;
    MockExternalSystemAccountIDHelper externalSystemAccountIDHelperMock;
    MockExternalSystemAccountIDPoolEntryHelper externalSystemAccountIDPoolEntryHelperMock;

    Operation operation;
    operation.body = Operation::_body_t(
        stellar::OperationType::BIND_EXTERNAL_SYSTEM_ACCOUNT_ID);
    OperationResult operationResult;

    ON_CALL(appMock, getDatabase()).WillByDefault(::testing::ReturnRef(dbMock));

    SECTION("Happy path")
    {
        BindExternalSystemAccountIdOpFrame opFrame(operation, operationResult,
                                                   transactionFrameMock);

        // EXPECT_CALL( ... );
        opFrame.doApply(appMock, storageHelper, ledgerManagerMock);
    }

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
