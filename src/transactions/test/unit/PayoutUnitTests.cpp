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
#include "transactions/test/mocks/MockBalanceHelper.h"
#include "transactions/test/mocks/MockAssetHelper.h"
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

TEST_CASE("payout - unit test", "[tx][payout]")
{
    MockApplication appMock;
    MockLedgerManager ledgerManagerMock;
    MockTransactionFrame transactionFrameMock;
    MockLedgerDelta ledgerDeltaMock;
    MockDatabase dbMock;
    MockStorageHelper storageHelperMock;
    MockKeyValueHelper keyValueHelperMock;
    MockBalanceHelper balanceHelperMock;
    MockAssetHelper assetHelperMock;
    MockExternalSystemAccountIDHelper externalSystemAccountIDHelperMock;
    MockExternalSystemAccountIDPoolEntryHelper
            externalSystemAccountIDPoolEntryHelperMock;
    std::shared_ptr<MockSignatureValidator> signatureValidatorMock =
            std::make_shared<MockSignatureValidator>();
    medida::MetricsRegistry metricsRegistryFake;

    PayoutOp op;
    op.maxPayoutAmount = 10 * ONE;
    op.minPayoutAmount = ONE;
    op.asset = "LTC";
    auto balance = SecretKey::random();
    auto source = txtest::Account{SecretKey::random(), Salt(0)};
    auto sourceID = source.key.getPublicKey();
    op.sourceBalanceID = balance.getPublicKey();
    Operation operation;
    operation.body = Operation::_body_t(OperationType::PAYOUT);
    operation.body.payoutOp() = op;
    operation.sourceAccount.activate() = sourceID;
    OperationResult operationResult;
    operationResult.code(OperationResultCode::opINNER);
    operationResult.tr().type(OperationType::PAYOUT);

    AccountFrame::pointer accountFrameFake =
            AccountFrame::makeAuthOnlyAccount(*operation.sourceAccount);
    LedgerHeader ledgerHeaderFake;
    AssetCreationRequest request;
    request.code = op.asset;
    request.preissuedAssetSigner = sourceID;
    request.initialPreissuedAmount = ONE;
    request.maxIssuanceAmount = ONE;
    request.policies = 0;
    request.details =  "";
    AssetFrame::pointer assetFrameFake = AssetFrame::create(request, sourceID);

    request.code = "USD";
    request.policies = static_cast<uint32_t>(AssetPolicy::TRANSFERABLE);
    AssetFrame::pointer payAssetFrameFake = AssetFrame::create(request, sourceID);
    BalanceFrame::pointer balanceFrameFake = BalanceFrame::createNew(
            balance.getPublicKey(), sourceID, op.asset);

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
    ON_CALL(storageHelperMock, getKeyValueHelper())
            .WillByDefault(ReturnRef(keyValueHelperMock));
    ON_CALL(storageHelperMock, getBalanceHelper())
            .WillByDefault(ReturnRef(balanceHelperMock));
    ON_CALL(storageHelperMock, getAssetHelper())
            .WillByDefault(ReturnRef(assetHelperMock));
    ON_CALL(storageHelperMock, getExternalSystemAccountIDHelper())
            .WillByDefault(ReturnRef(externalSystemAccountIDHelperMock));
    ON_CALL(storageHelperMock, getExternalSystemAccountIDPoolEntryHelper())
            .WillByDefault(ReturnRef(externalSystemAccountIDPoolEntryHelperMock));

    PayoutOpFrame opFrame(operation, operationResult, transactionFrameMock);

    SECTION("Check validity")
    {
        EXPECT_CALL(transactionFrameMock,
                    loadAccount(&ledgerDeltaMock, Ref(dbMock),
                                sourceID))
                .WillOnce(Return(accountFrameFake));
        EXPECT_CALL(appMock,
                    getMetrics())
                .WillOnce(ReturnRef(metricsRegistryFake));
        REQUIRE_FALSE(opFrame.checkValid(appMock, &ledgerDeltaMock));
    }

    SECTION("Apply success do check valid")
    {
        REQUIRE(opFrame.doCheckValid(appMock));
        REQUIRE(opFrame.getResult().code() == OperationResultCode::opINNER);
        REQUIRE(opFrame.getResult().tr().payoutResult().code() ==
                PayoutResultCode::SUCCESS);
    }

    SECTION("Apply, no such asset")
    {
        REQUIRE_FALSE(
                opFrame.doApply(appMock, storageHelperMock, ledgerManagerMock));
        REQUIRE(opFrame.getResult().code() ==
                OperationResultCode::opINNER);
        REQUIRE(opFrame.getResult().tr().payoutResult().code() ==
                PayoutResultCode::ASSET_NOT_FOUND);
    }

    SECTION("Apply, no such balance")
    {
        EXPECT_CALL(assetHelperMock, loadAsset(op.asset, sourceID))
                .WillOnce(Return(assetFrameFake));
        REQUIRE_FALSE(
                opFrame.doApply(appMock, storageHelperMock, ledgerManagerMock));
        REQUIRE(opFrame.getResult().code() ==
                OperationResultCode::opINNER);
        REQUIRE(opFrame.getResult().tr().payoutResult().code() ==
                PayoutResultCode::BALANCE_NOT_FOUND);
    }

    /*SECTION("Apply, asset not transferable")
    {
        payAssetFrameFake->setPolicies(0);
        EXPECT_CALL(assetHelperMock, loadAsset(op.asset, sourceID))
                .WillOnce(Return(assetFrameFake));
        EXPECT_CALL(balanceHelperMock, loadBalance(balance.getPublicKey(), sourceID))
                .WillOnce(Return(balanceFrameFake));
        ON_CALL(assetHelperMock, mustLoadAsset(request.code))
                .WillByDefault(Return(payAssetFrameFake));
        REQUIRE_FALSE(
                opFrame.doApply(appMock, storageHelperMock, ledgerManagerMock));
        REQUIRE(opFrame.getResult().code() ==
                OperationResultCode::opINNER);
        REQUIRE(opFrame.getResult().tr().payoutResult().code() ==
                PayoutResultCode::ASSET_NOT_TRANSFERABLE);
    }*/

    /*SECTION("Apply, no asset holders")
    {
        EXPECT_CALL(assetHelperMock, loadAsset(op.asset, sourceID))
                .WillOnce(Return(assetFrameFake));
        EXPECT_CALL(balanceHelperMock, loadBalance(balance.getPublicKey(), sourceID))
                .WillOnce(Return(balanceFrameFake));
        REQUIRE_FALSE(
                opFrame.doApply(appMock, storageHelperMock, ledgerManagerMock));
        REQUIRE(opFrame.getResult().code() ==
                OperationResultCode::opINNER);
        REQUIRE(opFrame.getResult().tr().payoutResult().code() ==
                PayoutResultCode::HOLDERS_NOT_FOUND);
    }*/
}
