// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0
#include <transactions/test/test_helper/ManageInvoiceRequestTestHelper.h>
#include <ledger/BalanceHelper.h>
#include <transactions/test/test_helper/IssuanceRequestHelper.h>
#include <transactions/test/test_helper/ManageAssetTestHelper.h>
#include <transactions/test/test_helper/PaymentV2TestHelper.h>
#include <transactions/test/test_helper/SetFeesTestHelper.h>
#include <transactions/test/test_helper/ManageAssetPairTestHelper.h>
#include <transactions/test/test_helper/CreateAccountTestHelper.h>
#include <transactions/test/test_helper/BillPayTestHelper.h>
#include <transactions/test/test_helper/LimitsUpdateRequestHelper.h>
#include "main/Application.h"
#include "ledger/LedgerManager.h"
#include "overlay/LoopbackPeer.h"
#include "util/make_unique.h"
#include "main/test.h"
#include "TxTests.h"
#include "ledger/LedgerDelta.h"
#include "ledger/ReferenceFrame.h"
#include "test/test_marshaler.h"

#include "crypto/SHA.h"

using namespace stellar;
using namespace stellar::txtest;

typedef std::unique_ptr<Application> appPtr;

TEST_CASE("Bill pay", "[tx][bill_pay]")
{
    Config const& cfg = getTestConfig(0, Config::TESTDB_POSTGRESQL);
    VirtualClock clock;
    Application::pointer appPtr = Application::create(clock, cfg);
    Application& app = *appPtr;
    app.start();
    auto testManager = TestManager::make(app);

    upgradeToCurrentLedgerVersion(app);


    // test helpers
    auto createAccountTestHelper = CreateAccountTestHelper(testManager);
    auto issuanceTestHelper = IssuanceRequestHelper(testManager);
    auto manageAssetTestHelper = ManageAssetTestHelper(testManager);
    auto manageAssetPairTestHelper = ManageAssetPairTestHelper(testManager);
    auto paymentV2TestHelper = PaymentV2TestHelper(testManager);
    auto setFeesTestHelper = SetFeesTestHelper(testManager);
    ManageInvoiceRequestTestHelper manageInvoiceRequestTestHelper(testManager);
    BillPayTestHelper billPayTestHelper(testManager);

    // set up world
    auto balanceHelper = BalanceHelper::Instance();
    Database &db = testManager->getDB();

    auto root = Account{getRoot(), Salt(0)};
    auto payer = Account{SecretKey::random(), Salt(1)};
    auto recipient = Account{SecretKey::random(), Salt(1)};

    // create two assets
    AssetCode paymentAsset = "USD";
    issuanceTestHelper.createAssetWithPreIssuedAmount(root, paymentAsset, INT64_MAX, root);
    manageAssetTestHelper.updateAsset(root, paymentAsset, root, static_cast<uint32_t>(AssetPolicy::BASE_ASSET) |
                                                                static_cast<uint32_t>(AssetPolicy::TRANSFERABLE) |
                                                                static_cast<uint32_t>(AssetPolicy::STATS_QUOTE_ASSET)
    );
    AssetCode feeAsset = "ETH";
    issuanceTestHelper.createAssetWithPreIssuedAmount(root, feeAsset, INT64_MAX, root);
    manageAssetTestHelper.updateAsset(root, feeAsset, root, static_cast<uint32_t>(AssetPolicy::BASE_ASSET) |
                                                            static_cast<uint32_t>(AssetPolicy::TRANSFERABLE)
    );

    // create payment participants
    createAccountTestHelper.applyCreateAccountTx(root, payer.key.getPublicKey(), AccountType::GENERAL);
    createAccountTestHelper.applyCreateAccountTx(root, recipient.key.getPublicKey(), AccountType::GENERAL);

    // exchange rates ETH USD
    auto exchangeRatesETH_USD = 5;

    // create asset pair
    manageAssetPairTestHelper.createAssetPair(root, paymentAsset, feeAsset, exchangeRatesETH_USD * ONE);

    // create fee charging rules for incoming and outgoing payments
    auto incomingFee = setFeesTestHelper.createFeeEntry(FeeType::PAYMENT_FEE, paymentAsset, 5 * ONE, 0, nullptr, nullptr,
                                                        static_cast<int64_t>(PaymentFeeType::INCOMING), 0, INT64_MAX,
                                                        &paymentAsset);
    setFeesTestHelper.applySetFeesTx(root, &incomingFee, false);

    auto outgoingFee = setFeesTestHelper.createFeeEntry(FeeType::PAYMENT_FEE, paymentAsset, 5 * ONE, 5 * ONE, nullptr, nullptr,
                                                        static_cast<int64_t>(PaymentFeeType::OUTGOING), 0, INT64_MAX,
                                                        &paymentAsset);
    setFeesTestHelper.applySetFeesTx(root, &outgoingFee, false);

    // fund payer
    auto payerBalance = balanceHelper->loadBalance(payer.key.getPublicKey(), paymentAsset, db, nullptr);
    REQUIRE(!!payerBalance);

    auto receiverBalance = balanceHelper->loadBalance(recipient.key.getPublicKey(), paymentAsset, db, nullptr);
    REQUIRE(!!receiverBalance);

    auto payerFeeBalance = balanceHelper->loadBalance(payer.key.getPublicKey(), feeAsset, db, nullptr);
    REQUIRE(!!payerFeeBalance);

    uint64_t paymentAmount = 100 * ONE;
    auto emissionAmount = 3 * paymentAmount;

    issuanceTestHelper.applyCreateIssuanceRequest(root, paymentAsset, emissionAmount, payerBalance->getBalanceID(),
                                                  SecretKey::random().getStrKeyPublic());
    issuanceTestHelper.applyCreateIssuanceRequest(root, feeAsset, emissionAmount, payerFeeBalance->getBalanceID(),
                                                  SecretKey::random().getStrKeyPublic());

    // create destination and feeData for further tests
    auto destination = paymentV2TestHelper.createDestinationForAccount(recipient.key.getPublicKey());

    auto sourceFeeData = paymentV2TestHelper.createFeeData(outgoingFee.fixedFee, outgoingFee.percentFee,
                                                           outgoingFee.ext.feeAsset());
    auto destFeeData = paymentV2TestHelper.createFeeData(incomingFee.fixedFee, incomingFee.percentFee,
                                                         incomingFee.ext.feeAsset());
    auto paymentFeeData = paymentV2TestHelper.createPaymentFeeData(sourceFeeData, destFeeData, true);

	longstring details = " Bill for certain services";

	SECTION("Success create invoice request")
    {
        auto createInvoiceRequestOp = manageInvoiceRequestTestHelper.createInvoiceRequest(
                receiverBalance->getBalanceID(), payer.key.getPublicKey(), paymentAmount, details);

        auto result = manageInvoiceRequestTestHelper.applyManageInvoiceRequest(recipient, createInvoiceRequestOp);
        auto requestID = result.success().details.response().requestID;
        REQUIRE(result.success().details.response().asset == paymentAsset);
        REQUIRE(result.success().details.response().senderBalance == payerBalance->getBalanceID());

        SECTION("Success bill pay")
        {
            auto opResult = billPayTestHelper.applyBillPayTx(payer, requestID, payerBalance->getBalanceID(),
                    destination, paymentAmount, paymentFeeData, "", "", nullptr);
        }

        SECTION("Invoice request not found")
        {
            billPayTestHelper.applyBillPayTx(payer, 123027538, payerBalance->getBalanceID(),
                                             destination, paymentAmount, paymentFeeData, "", "", nullptr,
                                             BillPayResultCode::INVOICE_REQUEST_NOT_FOUND);
        }

        SECTION("Amount mismatched")
        {
            billPayTestHelper.applyBillPayTx(payer, requestID, payerBalance->getBalanceID(),
                                             destination, paymentAmount + 1, paymentFeeData, "", "", nullptr,
                                             BillPayResultCode::AMOUNT_MISMATCHED);
            billPayTestHelper.applyBillPayTx(payer, requestID, payerBalance->getBalanceID(),
                                             destination, paymentAmount - 1, paymentFeeData, "", "", nullptr,
                                             BillPayResultCode::AMOUNT_MISMATCHED);
        }

        SECTION("Destination account mismatched")
        {
            destination = paymentV2TestHelper.createDestinationForAccount(SecretKey::random().getPublicKey());
            billPayTestHelper.applyBillPayTx(payer, requestID, payerBalance->getBalanceID(),
                                             destination, paymentAmount, paymentFeeData, "", "", nullptr,
                                             BillPayResultCode::DESTINATION_ACCOUNT_MISMATCHED);
        }

        SECTION("Destination balance mismatched")
        {
            destination = paymentV2TestHelper.createDestinationForBalance(SecretKey::random().getPublicKey());
            billPayTestHelper.applyBillPayTx(payer, requestID, payerBalance->getBalanceID(),
                                             destination, paymentAmount, paymentFeeData, "", "", nullptr,
                                             BillPayResultCode::DESTINATION_BALANCE_MISMATCHED);
        }

        SECTION("destination cannot pay fee for billpay")
        {
            paymentFeeData = paymentV2TestHelper.createPaymentFeeData(sourceFeeData, destFeeData, false);
            billPayTestHelper.applyBillPayTx(payer, requestID, payerBalance->getBalanceID(),
                                             destination, paymentAmount, paymentFeeData, "", "", nullptr,
                                             BillPayResultCode::REQUIRED_SOURCE_PAY_FOR_DESTINATION);
        }

        SECTION("Reference duplication")
        {
            manageInvoiceRequestTestHelper.applyManageInvoiceRequest(recipient, createInvoiceRequestOp,
                         ManageInvoiceRequestResultCode::MANAGE_INVOICE_REQUEST_REFERENCE_DUPLICATION);
        }

        SECTION("Too many invoices")
        {
            for (int i = 1; i < app.getMaxInvoicesForReceiverAccount(); i++)
            {
                createInvoiceRequestOp = manageInvoiceRequestTestHelper.createInvoiceRequest(
                        receiverBalance->getBalanceID(), payer.key.getPublicKey(), paymentAmount + i,
                        details + std::to_string(i));

                manageInvoiceRequestTestHelper.applyManageInvoiceRequest(recipient, createInvoiceRequestOp);
            }

            createInvoiceRequestOp = manageInvoiceRequestTestHelper.createInvoiceRequest(
                    receiverBalance->getBalanceID(), payer.key.getPublicKey(), paymentAmount + 23,
                    "expected to be excess");

            manageInvoiceRequestTestHelper.applyManageInvoiceRequest(recipient, createInvoiceRequestOp,
                    ManageInvoiceRequestResultCode::TOO_MANY_INVOICES);
        }

        SECTION("Success remove")
        {
            auto removeInvoiceRequestOp = manageInvoiceRequestTestHelper.createRemoveInvoiceRequest(requestID);

            manageInvoiceRequestTestHelper.applyManageInvoiceRequest(recipient, removeInvoiceRequestOp);

            SECTION("Already removed")
            {
                manageInvoiceRequestTestHelper.applyManageInvoiceRequest(recipient, removeInvoiceRequestOp,
                                         ManageInvoiceRequestResultCode::NOT_FOUND);
            }
        }
    }

	SECTION("Unsuccessful create invoice request")
	{
	    SECTION("Malformed")
        {
            auto createInvoiceRequestOp = manageInvoiceRequestTestHelper.createInvoiceRequest(
                    receiverBalance->getBalanceID(), payer.key.getPublicKey(), 0, details);

            manageInvoiceRequestTestHelper.applyManageInvoiceRequest(recipient, createInvoiceRequestOp,
                    ManageInvoiceRequestResultCode::MALFORMED);
        }

        SECTION("WRONG RECEIVE BALANCE")
        {
            auto createInvoiceRequestOp = manageInvoiceRequestTestHelper.createInvoiceRequest(
                    SecretKey::random().getPublicKey(), payer.key.getPublicKey(), paymentAmount, details);

            manageInvoiceRequestTestHelper.applyManageInvoiceRequest(recipient, createInvoiceRequestOp,
                    ManageInvoiceRequestResultCode::BALANCE_NOT_FOUND);
        }

        SECTION("Destination balance not found")
        {
            auto createInvoiceRequestOp = manageInvoiceRequestTestHelper.createInvoiceRequest(
                    receiverBalance->getBalanceID(), SecretKey::random().getPublicKey(), paymentAmount, details);

            manageInvoiceRequestTestHelper.applyManageInvoiceRequest(recipient, createInvoiceRequestOp,
                                                                                   ManageInvoiceRequestResultCode::BALANCE_NOT_FOUND);
        }

        SECTION("Request not found")
        {
            uint64_t notExistingRequestID = 123;
            auto removeInvoiceRequestOp = manageInvoiceRequestTestHelper.createRemoveInvoiceRequest(notExistingRequestID);

            manageInvoiceRequestTestHelper.applyManageInvoiceRequest(recipient, removeInvoiceRequestOp,
                    ManageInvoiceRequestResultCode::NOT_FOUND);
        }

        SECTION("Request not found")
        {
            auto limitsUpdateRequestHelper = LimitsUpdateRequestHelper(testManager);
            auto limitsUpdateRequest = limitsUpdateRequestHelper.createLimitsUpdateRequest("limitsRequestData");
            auto limitsUpdateResult = limitsUpdateRequestHelper.applyCreateLimitsUpdateRequest(payer,
                    limitsUpdateRequest);

            auto removeInvoiceRequestOp = manageInvoiceRequestTestHelper.createRemoveInvoiceRequest(
                    limitsUpdateResult.success().manageLimitsRequestID);

            manageInvoiceRequestTestHelper.applyManageInvoiceRequest(recipient, removeInvoiceRequestOp,
                                                                     ManageInvoiceRequestResultCode::NOT_FOUND);
        }
	}
}
