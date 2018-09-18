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
#include <ledger/ContractHelper.h>
#include <transactions/test/test_helper/ManageContractTestHelper.h>
#include <ledger/ReviewableRequestHelper.h>
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

TEST_CASE("Contract", "[tx][contract]")
{
    Config const &cfg = getTestConfig(0, Config::TESTDB_POSTGRESQL);
    VirtualClock clock;
    Application::pointer appPtr = Application::create(clock, cfg);
    Application &app = *appPtr;
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
    ManageContractRequestTestHelper manageContractRequestTestHelper(testManager);
    ReviewContractRequestHelper reviewContractRequestHelper(testManager);
    ManageContractTestHelper manageContractTestHelper(testManager);

    // set up world
    auto balanceHelper = BalanceHelperLegacy::Instance();
    Database &db = testManager->getDB();

    auto root = Account{getRoot(), Salt(0)};
    auto payer = Account{SecretKey::random(), Salt(1)};
    auto recipient = Account{SecretKey::random(), Salt(1)};

    // create two assets
    AssetCode paymentAsset = "USD";
    issuanceTestHelper.createAssetWithPreIssuedAmount(root, paymentAsset, UINT64_MAX, root);
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
    auto incomingFee = setFeesTestHelper.createFeeEntry(FeeType::PAYMENT_FEE, paymentAsset, 5 * ONE, 0, nullptr,
                                                        nullptr,
                                                        static_cast<int64_t>(PaymentFeeType::INCOMING), 0, INT64_MAX,
                                                        &paymentAsset);
    setFeesTestHelper.applySetFeesTx(root, &incomingFee, false);

    auto outgoingFee = setFeesTestHelper.createFeeEntry(FeeType::PAYMENT_FEE, paymentAsset, 5 * ONE, 5 * ONE, nullptr,
                                                        nullptr,
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
    auto destination = paymentV2TestHelper.createDestinationForAccount(recipient.key.getPublicKey());

    auto sourceFeeData = paymentV2TestHelper.createFeeData(outgoingFee.fixedFee, outgoingFee.percentFee,
                                                           outgoingFee.ext.feeAsset());
    auto destFeeData = paymentV2TestHelper.createFeeData(incomingFee.fixedFee, incomingFee.percentFee,
                                                         incomingFee.ext.feeAsset());
    auto paymentFeeData = paymentV2TestHelper.createPaymentFeeData(sourceFeeData, destFeeData, true);

    longstring details = "Contract details";

    SECTION("Success create contract request") {
        uint64_t startTime = testManager->getLedgerManager().getCloseTime() + 1234;
        uint64_t endTime = testManager->getLedgerManager().getCloseTime() + ONE;
        auto createContractRequestOp = manageContractRequestTestHelper.createContractRequest(
                payer.key.getPublicKey(), root.key.getPublicKey(), startTime, endTime, details);

        auto result = manageContractRequestTestHelper.applyManageContractRequest(recipient, createContractRequestOp);
        auto requestID = result.success().details.response().requestID;

        SECTION("Approve contract request without customer details")
        {
            auto reviewResult = reviewContractRequestHelper.applyReviewRequestTx(payer, requestID,
                                                                                 ReviewRequestOpAction::APPROVE, "");
            auto contractID = reviewResult.success().ext.contractID();
            auto contractHelper = ContractHelper::Instance();
            auto contractFrame = contractHelper->loadContract(contractID, db);
            REQUIRE(!!contractFrame);
            REQUIRE(contractFrame->getContractor() == recipient.key.getPublicKey());
            REQUIRE(contractFrame->getCustomer() == payer.key.getPublicKey());
            REQUIRE(contractFrame->getEscrow() == root.key.getPublicKey());
            REQUIRE(contractFrame->getStartTime() == startTime);
            REQUIRE(contractFrame->getEndTime() == endTime);
        }

        SECTION("Approve contract request") {
            reviewContractRequestHelper.customerDetails = "Some details, all okay.";
            auto reviewResult = reviewContractRequestHelper.applyReviewRequestTx(payer, requestID,
                                                                                 ReviewRequestOpAction::APPROVE, "");
            auto contractID = reviewResult.success().ext.contractID();
            auto contractHelper = ContractHelper::Instance();
            auto contractFrame = contractHelper->loadContract(contractID, db);
            REQUIRE(!!contractFrame);
            REQUIRE(contractFrame->getContractor() == recipient.key.getPublicKey());
            REQUIRE(contractFrame->getCustomer() == payer.key.getPublicKey());
            REQUIRE(contractFrame->getEscrow() == root.key.getPublicKey());
            REQUIRE(contractFrame->getStartTime() == startTime);
            REQUIRE(contractFrame->getEndTime() == endTime);

            SECTION("Only contractor can add invoice with contract") {
                auto localDetails = details + " not success";
                auto createInvoiceRequestOp = manageInvoiceRequestTestHelper.createInvoiceRequest(
                        paymentAsset, root.key.getPublicKey(), paymentAmount, localDetails, &contractID);

                auto invoiceResult = manageInvoiceRequestTestHelper.applyManageInvoiceRequest(recipient,
                                                                                              createInvoiceRequestOp,
                                                                                              ManageInvoiceRequestResultCode::SENDER_ACCOUNT_MISMATCHED);
            }

            SECTION("Only contractor can add invoice with contract") {
                auto localDetails = details + " not success";
                auto createInvoiceRequestOp = manageInvoiceRequestTestHelper.createInvoiceRequest(
                        paymentAsset, payer.key.getPublicKey(), paymentAmount, localDetails, &contractID);

                auto invoiceResult = manageInvoiceRequestTestHelper.applyManageInvoiceRequest(root,
                                                                                              createInvoiceRequestOp,
                                                                                              ManageInvoiceRequestResultCode::ONLY_CONTRACTOR_CAN_ATTACH_INVOICE_TO_CONTRACT);
            }

            auto createInvoiceRequestOp = manageInvoiceRequestTestHelper.createInvoiceRequest(
                    paymentAsset, payer.key.getPublicKey(), paymentAmount, details, &contractID);

            auto invoiceResult = manageInvoiceRequestTestHelper.applyManageInvoiceRequest(recipient,
                                                                                          createInvoiceRequestOp);
            auto invoiceRequestID = invoiceResult.success().details.response().requestID;
            REQUIRE(invoiceResult.success().details.response().receiverBalance == receiverBalance->getBalanceID());
            REQUIRE(invoiceResult.success().details.response().senderBalance == payerBalance->getBalanceID());
            std::vector<uint64_t> invoiceRequestsIDs;
            invoiceRequestsIDs.emplace_back(invoiceRequestID);
            auto invoiceRequests = ReviewableRequestHelper::Instance()->loadRequests(invoiceRequestsIDs, db);
            REQUIRE(invoiceRequestsIDs.size() == invoiceRequestsIDs.size());

            SECTION("Not allowed confirm contract with not approved invoices") {
                auto confirmOp = manageContractTestHelper.createConfirmOp(contractID);
                manageContractTestHelper.applyManageContractTx(recipient, confirmOp,
                                                               ManageContractResultCode::INVOICE_NOT_APPROVED);
                confirmOp = manageContractTestHelper.createConfirmOp(contractID);
                manageContractTestHelper.applyManageContractTx(payer, confirmOp,
                                                               ManageContractResultCode::INVOICE_NOT_APPROVED);
            }

            SECTION("Add details to contract") {
                auto addDetailsOp = manageContractTestHelper.createAddDetailsOp(contractID, details);
                manageContractTestHelper.applyManageContractTx(recipient, addDetailsOp);
                addDetailsOp = manageContractTestHelper.createAddDetailsOp(contractID, details);
                manageContractTestHelper.applyManageContractTx(recipient, addDetailsOp);
            }

            SECTION("Not allowed to add details to contract") {
                auto addDetailsOp = manageContractTestHelper.createAddDetailsOp(contractID, details);
                manageContractTestHelper.applyManageContractTx(root, addDetailsOp,
                                                               ManageContractResultCode::NOT_ALLOWED);
            }

            SECTION("Malformed")
            {
                longstring malformedDetails = "";
                auto addDetailsOp = manageContractTestHelper.createAddDetailsOp(contractID, malformedDetails);
                manageContractTestHelper.applyManageContractTx(root, addDetailsOp,
                                                               ManageContractResultCode::MALFORMED);
            }

            SECTION("Approve invoice with contract")
            {
                destination = paymentV2TestHelper.createDestinationForBalance(receiverBalance->getBalanceID());
                reviewInvoiceRequestHelper.initializePaymentDetails(destination, paymentAmount, paymentFeeData,
                                                                    "", "", payerBalance->getBalanceID());
                reviewInvoiceRequestHelper.applyReviewRequestTx(payer, invoiceRequestID,
                                                                ReviewRequestOpAction::APPROVE, "");

                SECTION("Confirm contract") {
                    auto confirmOp = manageContractTestHelper.createConfirmOp(contractID);
                    auto res = manageContractTestHelper.applyManageContractTx(recipient, confirmOp);

                    REQUIRE(!res.response().data.isCompleted());

                    confirmOp = manageContractTestHelper.createConfirmOp(contractID);
                    res = manageContractTestHelper.applyManageContractTx(payer, confirmOp);

                    REQUIRE(res.response().data.isCompleted());
                    REQUIRE(!contractHelper->exists(db, contractFrame->getKey()));
                    REQUIRE(!ReviewableRequestHelper::Instance()->loadRequest(invoiceRequestID, db));
                }

                SECTION("Start dispute") {
                    auto startDisputeOp = manageContractTestHelper.createStartDisputeOp(contractID,
                                                                                        "Some reason");
                    manageContractTestHelper.applyManageContractTx(recipient, startDisputeOp);

                    auto addDetailsOp = manageContractTestHelper.createAddDetailsOp(contractID, details);
                    manageContractTestHelper.applyManageContractTx(root, addDetailsOp);

                    SECTION("Resolve dispute (revert)") {
                        auto resolveDisputeOp = manageContractTestHelper.createResolveDisputeOp(contractID, true);
                        manageContractTestHelper.applyManageContractTx(root, resolveDisputeOp);
                    }

                    SECTION("Resolve dispute (not revert)")
                    {
                        auto resolveDisputeOp = manageContractTestHelper.createResolveDisputeOp(contractID, false);
                        manageContractTestHelper.applyManageContractTx(root, resolveDisputeOp);
                    }
                }
            }

            SECTION("Start dispute") {
                auto startDisputeOp = manageContractTestHelper.createStartDisputeOp(contractID,
                                                                                    "Some reason");
                manageContractTestHelper.applyManageContractTx(recipient, startDisputeOp);

                auto addDetailsOp = manageContractTestHelper.createAddDetailsOp(contractID, details);
                manageContractTestHelper.applyManageContractTx(root, addDetailsOp);

                SECTION("Resolve dispute (revert)") {
                    auto resolveDisputeOp = manageContractTestHelper.createResolveDisputeOp(contractID, true);
                    manageContractTestHelper.applyManageContractTx(root, resolveDisputeOp);
                }

                SECTION("Resolve dispute (not revert)") {
                    auto resolveDisputeOp = manageContractTestHelper.createResolveDisputeOp(contractID, false);
                    manageContractTestHelper.applyManageContractTx(root, resolveDisputeOp);
                }
            }
        }

        SECTION("Only sender allowed to approve") {
            auto reviewResult = reviewContractRequestHelper.applyReviewRequestTx(root, requestID,
                                                                                 ReviewRequestOpAction::APPROVE, "",
                                                                                 ReviewRequestResultCode::NOT_FOUND);
        }

        SECTION("Only sender allowed to permanent reject") {
            auto reviewResult = reviewContractRequestHelper.applyReviewRequestTx(root, requestID,
                                                                                 ReviewRequestOpAction::PERMANENT_REJECT,
                                                                                 "Disagree with such amount",
                                                                                 ReviewRequestResultCode::NOT_FOUND);
        }

        SECTION("Success permanent reject") {
            auto reviewResult = reviewContractRequestHelper.applyReviewRequestTx(payer, requestID,
                                                                                 ReviewRequestOpAction::PERMANENT_REJECT,
                                                                                 "Some reason");
        }

        SECTION("Success remove contract request")
        {
            auto removeContractRequestOp = manageContractRequestTestHelper.createRemoveContractRequest(requestID);
            manageContractRequestTestHelper.applyManageContractRequest(recipient, removeContractRequestOp);

            SECTION("Already removed")
            {
                manageContractRequestTestHelper.applyManageContractRequest(recipient, removeContractRequestOp,
                                                                           ManageContractRequestResultCode::NOT_FOUND);
            }
        }
    }
}
