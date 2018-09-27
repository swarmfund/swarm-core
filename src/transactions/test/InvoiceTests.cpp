// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0
#include <transactions/test/test_helper/ManageInvoiceRequestTestHelper.h>
#include <ledger/BalanceHelperLegacy.h>
#include <transactions/test/test_helper/IssuanceRequestHelper.h>
#include <transactions/test/test_helper/ManageAssetTestHelper.h>
#include <transactions/test/test_helper/PaymentV2TestHelper.h>
#include <transactions/test/test_helper/SetFeesTestHelper.h>
#include <transactions/test/test_helper/ManageAssetPairTestHelper.h>
#include <transactions/test/test_helper/CreateAccountTestHelper.h>
#include <transactions/test/test_helper/LimitsUpdateRequestHelper.h>
#include <transactions/test/test_helper/ReviewInvoiceRequestHelper.h>
#include <transactions/test/test_helper/ManageContractRequestTestHelper.h>
#include <transactions/test/test_helper/ReviewContractRequestHelper.h>
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

TEST_CASE("Invoice", "[tx][invoice]")
{
    Config const& cfg = getTestConfig(0, Config::TESTDB_POSTGRESQL);
    VirtualClock clock;
    Application::pointer appPtr = Application::create(clock, cfg);
    Application& app = *appPtr;
    app.start();
    auto testManager = TestManager::make(app);
    TestManager::upgradeToCurrentLedgerVersion(app);

    // test helpers
    auto createAccountTestHelper = CreateAccountTestHelper(testManager);
    auto issuanceTestHelper = IssuanceRequestHelper(testManager);
    auto manageAssetTestHelper = ManageAssetTestHelper(testManager);
    auto manageAssetPairTestHelper = ManageAssetPairTestHelper(testManager);
    auto paymentV2TestHelper = PaymentV2TestHelper(testManager);
    auto setFeesTestHelper = SetFeesTestHelper(testManager);
    ManageInvoiceRequestTestHelper manageInvoiceRequestTestHelper(testManager);
    ReviewInvoiceRequestHelper reviewInvoiceRequestHelper(testManager);

    // set up world
    auto balanceHelper = BalanceHelperLegacy::Instance();
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

    uint32_t issuanceTasks = 0;

    issuanceTestHelper.applyCreateIssuanceRequest(root, paymentAsset, emissionAmount, payerBalance->getBalanceID(),
                                                  SecretKey::random().getStrKeyPublic(), &issuanceTasks);
    issuanceTestHelper.applyCreateIssuanceRequest(root, feeAsset, emissionAmount, payerFeeBalance->getBalanceID(),
                                                  SecretKey::random().getStrKeyPublic(), &issuanceTasks);

    // create destination and feeData for further tests
    auto destination = paymentV2TestHelper.createDestinationForBalance(receiverBalance->getBalanceID());

    auto sourceFeeData = paymentV2TestHelper.createFeeData(outgoingFee.fixedFee, outgoingFee.percentFee,
                                                           outgoingFee.ext.feeAsset());
    auto destFeeData = paymentV2TestHelper.createFeeData(incomingFee.fixedFee, incomingFee.percentFee,
                                                         incomingFee.ext.feeAsset());
    auto paymentFeeData = paymentV2TestHelper.createPaymentFeeData(sourceFeeData, destFeeData, true);

	longstring details = " Bill for certain services";

    SECTION("Success create invoice request")
    {
        auto createInvoiceRequestOp = manageInvoiceRequestTestHelper.createInvoiceRequest(
                paymentAsset, payer.key.getPublicKey(), paymentAmount, details);

        auto result = manageInvoiceRequestTestHelper.applyManageInvoiceRequest(recipient, createInvoiceRequestOp);
        auto requestID = result.success().details.response().requestID;
        REQUIRE(result.success().details.response().receiverBalance == receiverBalance->getBalanceID());
        REQUIRE(result.success().details.response().senderBalance == payerBalance->getBalanceID());

        reviewInvoiceRequestHelper.initializePaymentDetails(destination, paymentAmount, paymentFeeData,
                                                            "", "", payerBalance->getBalanceID());

        SECTION("Approve invoice request")
        {
            auto reviewResult = reviewInvoiceRequestHelper.applyReviewRequestTx(payer, requestID,
                                                                                ReviewRequestOpAction::APPROVE, "");
        }

        SECTION("Only sender allowed to approve")
        {
            auto reviewResult = reviewInvoiceRequestHelper.applyReviewRequestTx(root, requestID,
                    ReviewRequestOpAction::APPROVE, "", ReviewRequestResultCode::NOT_FOUND);
        }

        SECTION("Only sender allowed to permanent reject")
        {
            auto reviewResult = reviewInvoiceRequestHelper.applyReviewRequestTx(root, requestID,
                    ReviewRequestOpAction::PERMANENT_REJECT, "Some reason", ReviewRequestResultCode::NOT_FOUND);
        }

        SECTION("Success permanent reject")
        {
            auto reviewResult = reviewInvoiceRequestHelper.applyReviewRequestTx(payer, requestID,
                    ReviewRequestOpAction::PERMANENT_REJECT, "Some reason");
        }

        SECTION("Amount mismatched")
        {
            reviewInvoiceRequestHelper.initializePaymentDetails(destination, paymentAmount + 1, paymentFeeData,
                                                                "", "", payerBalance->getBalanceID());
            reviewInvoiceRequestHelper.applyReviewRequestTx(payer, requestID, ReviewRequestOpAction::APPROVE,
                                                            "", ReviewRequestResultCode::AMOUNT_MISMATCHED);

            reviewInvoiceRequestHelper.initializePaymentDetails(destination, paymentAmount - 1, paymentFeeData,
                                                                "", "", payerBalance->getBalanceID());
            reviewInvoiceRequestHelper.applyReviewRequestTx(payer, requestID, ReviewRequestOpAction::APPROVE,
                                                            "", ReviewRequestResultCode::AMOUNT_MISMATCHED);
        }

        SECTION("Destination account not allowed")
        {
            destination = paymentV2TestHelper.createDestinationForAccount(recipient.key.getPublicKey());
            reviewInvoiceRequestHelper.initializePaymentDetails(destination, paymentAmount, paymentFeeData,
                                                                "", "", payerBalance->getBalanceID());
            reviewInvoiceRequestHelper.applyReviewRequestTx(payer, requestID, ReviewRequestOpAction::APPROVE, "",
                                                            ReviewRequestResultCode::NOT_ALLOWED_ACCOUNT_DESTINATION);
        }

        SECTION("Destination balance mismatched")
        {
            destination = paymentV2TestHelper.createDestinationForBalance(SecretKey::random().getPublicKey());
            reviewInvoiceRequestHelper.initializePaymentDetails(destination, paymentAmount, paymentFeeData,
                                                                "", "", payerBalance->getBalanceID());
            reviewInvoiceRequestHelper.applyReviewRequestTx(payer, requestID, ReviewRequestOpAction::APPROVE, "",
                                                            ReviewRequestResultCode::DESTINATION_BALANCE_MISMATCHED);
        }

        SECTION("destination cannot pay fee for bill pay")
        {
            paymentFeeData = paymentV2TestHelper.createPaymentFeeData(sourceFeeData, destFeeData, false);
            reviewInvoiceRequestHelper.initializePaymentDetails(destination, paymentAmount, paymentFeeData,
                                                                "", "", payerBalance->getBalanceID());
            reviewInvoiceRequestHelper.applyReviewRequestTx(payer, requestID, ReviewRequestOpAction::APPROVE, "",
                                                            ReviewRequestResultCode::REQUIRED_SOURCE_PAY_FOR_DESTINATION);
        }

        SECTION("SOURCE BALANCE MISMATCHED")
        {
            reviewInvoiceRequestHelper.initializePaymentDetails(destination, paymentAmount, paymentFeeData,
                                                                "", "", SecretKey::random().getPublicKey());
            reviewInvoiceRequestHelper.applyReviewRequestTx(payer, requestID, ReviewRequestOpAction::APPROVE, "",
                                                            ReviewRequestResultCode::SOURCE_BALANCE_MISMATCHED);
        }

        SECTION("Too many invoices")
        {
            for (int i = 1; i < app.getMaxInvoicesForReceiverAccount(); i++)
            {
                createInvoiceRequestOp = manageInvoiceRequestTestHelper.createInvoiceRequest(
                        paymentAsset, payer.key.getPublicKey(), paymentAmount + i,
                        details + std::to_string(i));

                manageInvoiceRequestTestHelper.applyManageInvoiceRequest(recipient, createInvoiceRequestOp);
            }

            createInvoiceRequestOp = manageInvoiceRequestTestHelper.createInvoiceRequest(
                    paymentAsset, payer.key.getPublicKey(), paymentAmount + 23,
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

	SECTION("Unsuccessful manage invoice request")
	{
	    SECTION("Malformed")
        {
            auto createInvoiceRequestOp = manageInvoiceRequestTestHelper.createInvoiceRequest(
                    paymentAsset, payer.key.getPublicKey(), 0, details);

            manageInvoiceRequestTestHelper.applyManageInvoiceRequest(recipient, createInvoiceRequestOp,
                    ManageInvoiceRequestResultCode::MALFORMED);
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
            auto limitsUpdateRequest = limitsUpdateRequestHelper.createLimitsUpdateRequest(
                    "{\n \"a\": \"Limits request data\" \n}");
            auto limitsUpdateResult = limitsUpdateRequestHelper.applyCreateLimitsUpdateRequest(payer,
                    limitsUpdateRequest);

            auto removeInvoiceRequestOp = manageInvoiceRequestTestHelper.createRemoveInvoiceRequest(
                    limitsUpdateResult.success().manageLimitsRequestID);

            manageInvoiceRequestTestHelper.applyManageInvoiceRequest(recipient, removeInvoiceRequestOp,
                                                                     ManageInvoiceRequestResultCode::NOT_FOUND);
        }
	}
}
