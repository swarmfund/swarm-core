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
#include "transactions/test/mocks/MockApplication.h"
#include "transactions/test/mocks/MockDatabase.h"
#include "transactions/test/mocks/MockKeyValueHelper.h"
#include "transactions/test/mocks/MockLedgerDelta.h"
#include "transactions/test/mocks/MockLedgerManager.h"
#include "transactions/test/mocks/MockSignatureValidator.h"
#include "transactions/test/mocks/MockStorageHelper.h"
#include "transactions/test/mocks/MockExternalSystemAccountIDHelper.h"
#include "transactions/test/mocks/MockExternalSystemAccountIDPoolEntryHelper.h"
#include "transactions/test/mocks/MockTransactionFrame.h"
#include "transactions/PayoutOpFrame.h"
#include "util/StatusManager.h"
#include "util/Timer.h"
#include "util/TmpDir.h"
#include "work/WorkManager.h"

using namespace stellar;
using namespace testing;

static int32 externalSystemType = 5;
static uint256 sourceAccountPublicKey = hexToBin256(
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAABB");

TEST_CASE("payout - unit test", "[tx][payout]")
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

    PayoutOp op;
    op.maxPayoutAmount = 10 * ONE;
    op.minPayoutAmount = ONE;
    op.asset = "LTC";
    auto balance = SecretKey::random();
    op.sourceBalanceID = balance.getPublicKey();
    Operation operation;
    operation.body = Operation::_body_t(OperationType::PAYOUT);
    operation.body.payoutOp() = op;
    operation.sourceAccount =
            xdr::pointer<AccountID>(new AccountID(CryptoKeyType::KEY_TYPE_ED25519));
    operation.sourceAccount->ed25519() = sourceAccountPublicKey;
    OperationResult operationResult;

    AccountFrame::pointer accountFrameFake =
            AccountFrame::makeAuthOnlyAccount(*operation.sourceAccount);
    LedgerHeader ledgerHeaderFake;

    ON_CALL(appMock, getDatabase()).WillByDefault(ReturnRef(dbMock));
    ON_CALL(appMock, getLedgerManager())
            .WillByDefault(ReturnRef(ledgerManagerMock));
    ON_CALL(ledgerManagerMock, getCurrentLedgerHeader())
            .WillByDefault(ReturnRef(ledgerHeaderFake));
    ON_CALL(storageHelperMock, getDatabase()).WillByDefault(ReturnRef(dbMock));
    ON_CALL(storageHelperMock, getLedgerDelta())
            .WillByDefault(ReturnRef(ledgerDeltaMock));
    ON_CALL(transactionFrameMock, getSignatureValidator())
            .WillByDefault(Return(signatureValidatorMock));
    ON_CALL(storageHelperMock, getKeyValueHelper())
            .WillByDefault(ReturnRef(keyValueHelperMock));
    ON_CALL(storageHelperMock, getExternalSystemAccountIDHelper())
            .WillByDefault(ReturnRef(externalSystemAccountIDHelperMock));
    ON_CALL(storageHelperMock, getExternalSystemAccountIDPoolEntryHelper())
            .WillByDefault(ReturnRef(externalSystemAccountIDPoolEntryHelperMock));

    PayoutOpFrame opFrame(operation, operationResult, transactionFrameMock);

    SECTION("Check validity")
    {
        EXPECT_CALL(transactionFrameMock,
                    loadAccount(&ledgerDeltaMock, Ref(dbMock),
                                *operation.sourceAccount))
                .WillOnce(Return(accountFrameFake));
        REQUIRE(opFrame.checkValid(appMock, &ledgerDeltaMock));

        SECTION("Apply, no pool entry to bind")
        {
            REQUIRE_FALSE(
                    opFrame.doApply(appMock, ledgerDeltaMock, ledgerManagerMock));
            REQUIRE(opFrame.getResult().tr().payoutResult().code() ==
                    PayoutResultCode::BALANCE_NOT_FOUND);
        }
    }

}