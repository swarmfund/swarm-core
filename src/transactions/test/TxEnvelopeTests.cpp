// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "crypto/SHA.h"
#include "ledger/AccountHelper.h"
#include "ledger/BalanceHelperLegacy.h"
#include "ledger/LedgerDeltaImpl.h"
#include "main/Application.h"
#include "main/test.h"
#include "overlay/LoopbackPeer.h"
#include "test/test_marshaler.h"
#include "test_helper/CreateAccountTestHelper.h"
#include "test_helper/IssuanceRequestHelper.h"
#include "test_helper/ManageAssetTestHelper.h"
#include "test_helper/ManageKeyValueTestHelper.h"
#include "test_helper/SetFeesTestHelper.h"

using namespace stellar;
using namespace stellar::txtest;

typedef std::unique_ptr<Application> appPtr;

TEST_CASE("txenvelope", "[tx][envelope]")
{
    Config const& cfg = getTestConfig(0, Config::TESTDB_POSTGRESQL);

    VirtualClock clock;
    Application::pointer appPtr = Application::create(clock, cfg);
    Application& app = *appPtr;
    app.start();
    TestManager::upgradeToCurrentLedgerVersion(app);
    auto testManager = TestManager::make(app);
    Database& db = testManager->getDB();

    auto root = Account{getRoot(), Salt(0)};

    // test helpers
    auto createAccountTestHelper = CreateAccountTestHelper(testManager);
    auto issuanceTestHelper = IssuanceRequestHelper(testManager);
    auto manageAssetTestHelper = ManageAssetTestHelper(testManager);
    auto manageKeyValueTestHelper = ManageKeyValueTestHelper(testManager);
    auto setFeesTestHelper = SetFeesTestHelper(testManager);
    auto txHelper = TxHelper(testManager);

    // db helpers
    auto balanceHelper = BalanceHelperLegacy::Instance();

    SECTION("Transaction fee")
    {
        // create asset for tx fee
        AssetCode txFeeAssetCode = "ETH";
        uint64_t preIssuedAmount = 10000 * ONE;
        issuanceTestHelper.createAssetWithPreIssuedAmount(
            root, txFeeAssetCode, preIssuedAmount, root);

        // create source of tx
        auto txFeePayer = Account{SecretKey::random(), Salt(0)};
        createAccountTestHelper.applyCreateAccountTx(
            root, txFeePayer.key.getPublicKey(), AccountType::SYNDICATE);

        // create operation source for not tx source's operation
        auto operationSource = Account{SecretKey::random(), Salt(0)};
        createAccountTestHelper.applyCreateAccountTx(
            root, operationSource.key.getPublicKey(), AccountType::SYNDICATE);

        uint32_t issuanceTasks = 0;

        // issue tx fee asset to tx source
        auto txFeePayerBalanceBeforeTx = balanceHelper->mustLoadBalance(
            txFeePayer.key.getPublicKey(), txFeeAssetCode, db);
        issuanceTestHelper.applyCreateIssuanceRequest(
            root, txFeeAssetCode, preIssuedAmount / 2,
            txFeePayerBalanceBeforeTx->getBalanceID(),
            txFeePayer.key.getStrKeyPublic(), &issuanceTasks);

        // load tx fee payer balance one more time to have actual balance amount
        txFeePayerBalanceBeforeTx = balanceHelper->mustLoadBalance(
            txFeePayer.key.getPublicKey(), txFeeAssetCode, db);

        // create new key value for tx fee asset
        manageKeyValueTestHelper.setKey(
            ManageKeyValueOpFrame::transactionFeeAssetKey);
        manageKeyValueTestHelper.setValue(txFeeAssetCode);
        manageKeyValueTestHelper.setResult(ManageKeyValueResultCode::SUCCESS);
        manageKeyValueTestHelper.doApply(app, ManageKVAction::PUT, true,
                                         KeyValueEntryType::STRING);

        // create fee for ManageAssetOp
        auto txFee = 10 * ONE;
        auto manageAssetFee = setFeesTestHelper.createFeeEntry(
            FeeType::OPERATION_FEE, txFeeAssetCode, txFee / 2, 0, nullptr,
            nullptr, static_cast<int64_t>(OperationType::MANAGE_ASSET));
        setFeesTestHelper.applySetFeesTx(root, &manageAssetFee, false);

        // create two manage asset ops with different accounts
        AssetCode txFeePayerAssetToIssue = "FPC";
        auto txFeePayerAssetCreationRequest =
            manageAssetTestHelper.createAssetCreationRequest(
                txFeePayerAssetToIssue, txFeePayer.key.getPublicKey(), "{}",
                1000 * ONE, 0);
        auto feePayerManageAssetOp = manageAssetTestHelper.createManageAssetOp(
            txFeePayer, 0, txFeePayerAssetCreationRequest);

        AssetCode operationSourceAssetToIssue = "OSC";
        auto operationSourceAssetCreationRequest =
            manageAssetTestHelper.createAssetCreationRequest(
                operationSourceAssetToIssue, operationSource.key.getPublicKey(),
                "{}", 500 * ONE, 0);
        auto operationSourceManageAssetOp =
            manageAssetTestHelper.createManageAssetOp(
                operationSource, 0, operationSourceAssetCreationRequest);
        operationSourceManageAssetOp.sourceAccount.activate() =
            operationSource.key.getPublicKey();

        // store created ops in vector
        std::vector<Operation> ops;
        ops.push_back(feePayerManageAssetOp);
        ops.push_back(operationSourceManageAssetOp);

        SECTION("Insufficient fee")
        {
            uint64_t maxTotalFee = ONE;
            TransactionFramePtr txFrame =
                txHelper.txFromOperations(txFeePayer, ops, &maxTotalFee);
            txFrame->addSignature(operationSource.key);
            testManager->applyCheck(txFrame);
            auto txResult = txFrame->getResult();
            REQUIRE(txResult.result.code() ==
                    TransactionResultCode::txINSUFFICIENT_FEE);
        }

        SECTION("Source doesn't have balance in tx fee asset")
        {
            manageKeyValueTestHelper.setKey(ManageKeyValueOpFrame::transactionFeeAssetKey);
            manageKeyValueTestHelper.setValue("VLT");
            manageKeyValueTestHelper.setResult(ManageKeyValueResultCode::SUCCESS);
            manageKeyValueTestHelper.doApply(app, ManageKVAction::PUT, true,
                                             KeyValueEntryType::STRING);
            uint64_t maxTotalFee = 20 * ONE;
            TransactionFramePtr txFrame = txHelper.txFromOperations(txFeePayer, ops, &maxTotalFee);
            txFrame->addSignature(operationSource.key);
            testManager->applyCheck(txFrame);
            auto txResult = txFrame->getResult();
            REQUIRE(txResult.result.code() == TransactionResultCode::txSOURCE_UNDERFUNDED);
        }

        SECTION("Source underfunded")
        {
            manageAssetFee.fixedFee = preIssuedAmount;
            setFeesTestHelper.applySetFeesTx(root, &manageAssetFee, false);
            TransactionFramePtr txFrame =
                txHelper.txFromOperations(txFeePayer, ops);
            txFrame->addSignature(operationSource.key);
            testManager->applyCheck(txFrame);
            auto txResult = txFrame->getResult();
            REQUIRE(txResult.result.code() ==
                    TransactionResultCode::txSOURCE_UNDERFUNDED);
        }

        SECTION("Successful tx fee charge")
        {
            uint64_t maxTotalFee = 20 * ONE;
            TransactionFramePtr txFrame =
                txHelper.txFromOperations(txFeePayer, ops, &maxTotalFee);
            txFrame->addSignature(operationSource.key);
            testManager->applyCheck(txFrame);
            auto txResult = txFrame->getResult();
            REQUIRE(txResult.result.code() == TransactionResultCode::txSUCCESS);
            auto txFeePayerBalanceAfterTx = balanceHelper->mustLoadBalance(
                txFeePayerBalanceBeforeTx->getBalanceID(), db);
            REQUIRE(txFeePayerBalanceBeforeTx->getAmount() -
                        txFeePayerBalanceAfterTx->getAmount() ==
                    txFee);
        }
    }
}
