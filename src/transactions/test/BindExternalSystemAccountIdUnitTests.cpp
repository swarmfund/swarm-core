#include <exsysidgen/ETHIDGenerator.h>
#include "main/Application.h"
#include "util/Timer.h"
#include "main/Config.h"
#include "overlay/LoopbackPeer.h"
#include "main/test.h"
#include "TxTests.h"
#include "ledger/BalanceHelper.h"
#include "test_helper/BindExternalSystemAccountIdTestHelper.h"
#include "test_helper/CreateAccountTestHelper.h"
#include "test_helper/ManageExternalSystemAccountIDPoolEntryTestHelper.h"
#include "test/test_marshaler.h"

using namespace stellar;
using namespace stellar::txtest;

typedef std::unique_ptr<Application> appPtr;


TEST_CASE("bind external system account_id", "[tx][bind_external_system_account_id]")
{
    BindExternalSystemAccountIdOpFrame opFrame(dummy_operation, dummy_operation_frame, dummy_parent_tx_frame);

    SECTION("Happy path")
    {
        opFrame.doApply(dummy_app, dummy_ledger_delta, dummy_ledger_manager);
        EXPECT_CALL( ... );
    }

    SECTION("Checking validity")
    {
        opFrame.doCheckValid(dummy_app);
        EXPECT_CALL( ... );
    }
}

TEST_CASE("invalid BindExternalSystemAccountIdOpFrame", "[tx][invalid_bind_external_system_account_id_op_frame]")
{
    BindExternalSystemAccountIdOpFrame opFrame(dummy_operation, dummy_operation_frame, dummy_parent_tx_frame);

    SECTION("Checking validity")
    {
        EXPECT_FALSE(opFrame.doCheckValid(dummy_app));
    }
}
