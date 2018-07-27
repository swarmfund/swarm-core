#include "bucket/BucketManager.h"
#include "herder/Herder.h"
#include "invariant/Invariants.h"
#include "main/CommandHandler.h"
#include "main/PersistentState.h"
#include "medida/metrics_registry.h"
#include "overlay/BanManager.h"
#include "overlay/OverlayManager.h"
#include "process/ProcessManager.h"
#include "simulation/LoadGenerator.h"
#include "test/test_marshaler.h"
#include "transactions/BindExternalSystemAccountIdOpFrame.h"
#include "transactions/test/mocks/MockApplication.h"
#include "util/StatusManager.h"
#include "util/Timer.h"
#include "util/TmpDir.h"
#include "work/WorkManager.h"

using namespace stellar;
typedef std::unique_ptr<Application> appPtr;

TEST_CASE("bind external system account_id - unit test",
          "[tx][bind_external_system_account_id_unit_test]")
{
    /*
    MockOperation operationMock;
    MockOperationFrame operationFrameMock;
    MockTransactionFrame parentTxFrameMock;
    MockApplication appMock;
    MockLedgerDelta ledgerDeltaMock;
    MockLedgerManager ledgerManagerMock;

    SECTION("Happy path")
    {
        BindExternalSystemAccountIdOpFrame opFrame(operationMock,
    operationFrameMock, parentTxFrameMock);

        EXPECT_CALL( ... );
        opFrame.doApply(appMock, ledgerDeltaMock, ledgerManagerMock);
    }

    SECTION("Checking validity of valid frame")
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
    }
        */
    MockApplication mockApp;
    EXPECT_CALL(mockApp, getState());
}
