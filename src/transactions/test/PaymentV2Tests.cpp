#include <ledger/BalanceHelperLegacy.h>
#include <transactions/test/test_helper/CreateAccountTestHelper.h>
#include <transactions/test/test_helper/IssuanceRequestHelper.h>
#include <transactions/test/test_helper/ManageAssetTestHelper.h>
#include <transactions/test/test_helper/ManageAssetPairTestHelper.h>
#include <transactions/test/test_helper/PaymentV2TestHelper.h>
#include <transactions/test/test_helper/SetFeesTestHelper.h>
#include <transactions/test/test_helper/ManageLimitsTestHelper.h>
#include "test_helper/TxHelper.h"
#include "test/test_marshaler.h"
#include "main/test.h"
#include "TxTests.h"

using namespace stellar;
using namespace stellar::txtest;

typedef std::unique_ptr<Application> appPtr;

TEST_CASE("payment v2", "[tx][payment_v2]") {
    Config const &cfg = getTestConfig(0, Config::TESTDB_POSTGRESQL);

    VirtualClock clock;
    Application::pointer appPtr = Application::create(clock, cfg);
    Application &app = *appPtr;
    app.start();
    TestManager::upgradeToCurrentLedgerVersion(app);
    auto testManager = TestManager::make(app);
    Database &db = testManager->getDB();

    // test helpers
    auto createAccountTestHelper = CreateAccountTestHelper(testManager);
    auto issuanceTestHelper = IssuanceRequestHelper(testManager);
    auto manageAssetTestHelper = ManageAssetTestHelper(testManager);
    auto manageAssetPairTestHelper = ManageAssetPairTestHelper(testManager);
    auto paymentV2TestHelper = PaymentV2TestHelper(testManager);
    auto setFeesTestHelper = SetFeesTestHelper(testManager);
    auto manageLimitsTestHelper = ManageLimitsTestHelper(testManager);

    // db helpers
    auto balanceHelper = BalanceHelperLegacy::Instance();

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

    //create limits
    ManageLimitsOp manageLimitsOp;
    manageLimitsOp.details.action(ManageLimitsAction::CREATE);
    manageLimitsOp.details.limitsCreateDetails().accountID.activate() = payer.key.getPublicKey();
    manageLimitsOp.details.limitsCreateDetails().assetCode = "USD";
    manageLimitsOp.details.limitsCreateDetails().statsOpType = StatsOpType::PAYMENT_OUT;
    manageLimitsOp.details.limitsCreateDetails().isConvertNeeded = false;
    manageLimitsOp.details.limitsCreateDetails().dailyOut = 20000 * ONE;
    manageLimitsOp.details.limitsCreateDetails().weeklyOut = 40000 * ONE;
    manageLimitsOp.details.limitsCreateDetails().monthlyOut = 80000 * ONE;
    manageLimitsOp.details.limitsCreateDetails().annualOut = 200000 * ONE;
    manageLimitsTestHelper.applyManageLimitsTx(root, manageLimitsOp);

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
                                                        &feeAsset);
    setFeesTestHelper.applySetFeesTx(root, &outgoingFee, false);

    // fund payer
    auto payerBalance = balanceHelper->loadBalance(payer.key.getPublicKey(), paymentAsset, db, nullptr);
    REQUIRE(!!payerBalance);

    auto payerFeeBalance = balanceHelper->loadBalance(payer.key.getPublicKey(), feeAsset, db, nullptr);
    REQUIRE(!!payerFeeBalance);

    int64_t paymentAmount = 100 * ONE;
    auto emissionAmount = 3 * paymentAmount;

    uint32_t issuanceTasks = 0;

    issuanceTestHelper.applyCreateIssuanceRequest(root, paymentAsset, emissionAmount, payerBalance->getBalanceID(),
                                                  SecretKey::random().getStrKeyPublic(), &issuanceTasks);
    issuanceTestHelper.applyCreateIssuanceRequest(root, feeAsset, emissionAmount, payerFeeBalance->getBalanceID(),
                                                  SecretKey::random().getStrKeyPublic(), &issuanceTasks);

    // create destination and feeData for further tests
    auto destination = paymentV2TestHelper.createDestinationForAccount(recipient.key.getPublicKey());

    // maxPaymnetFee more in five times because fee in ETH, payment in USD, exchange rates 1:5
    auto sourceFeeData = paymentV2TestHelper.createFeeData(outgoingFee.fixedFee, outgoingFee.percentFee * exchangeRatesETH_USD,
                                                           outgoingFee.ext.feeAsset());
    auto destFeeData = paymentV2TestHelper.createFeeData(incomingFee.fixedFee, incomingFee.percentFee,
                                                         incomingFee.ext.feeAsset());
    auto paymentFeeData = paymentV2TestHelper.createPaymentFeeData(sourceFeeData, destFeeData, false);

    SECTION("Malformed") {
        SECTION("Reference longer than 64") {
            auto invalidReference = "VECUCAORAHYCZKCAWHIJYYQZAAUWHDRNJZZLBZWCZIOQJADHWMAANUWWQNXQLSHPR";
            auto opResult = paymentV2TestHelper.applyPaymentV2Tx(payer, payerBalance->getBalanceID(), destination,
                                                                 paymentAmount, paymentFeeData, "",
                                                                 invalidReference, nullptr,
                                                                 PaymentV2ResultCode::MALFORMED);
        }
        SECTION("Send to self by balance") {
            auto balanceDestination = paymentV2TestHelper.createDestinationForBalance(payerBalance->getBalanceID());
            auto opResult = paymentV2TestHelper.applyPaymentV2Tx(payer, payerBalance->getBalanceID(),
                                                                 balanceDestination, paymentAmount, paymentFeeData,
                                                                 "", "", nullptr,
                                                                 PaymentV2ResultCode::MALFORMED);
        }
        SECTION("Send to self by account") {
            auto accountDestination = paymentV2TestHelper.createDestinationForAccount(payer.key.getPublicKey());
            auto opResult = paymentV2TestHelper.applyPaymentV2Tx(payer, payerBalance->getBalanceID(),
                                                                 accountDestination, paymentAmount, paymentFeeData,
                                                                 "", "", nullptr,
                                                                 PaymentV2ResultCode::MALFORMED);
        }
        SECTION("Invalid asset code") {
            paymentFeeData.sourceFee.feeAsset = "";
            auto opResult = paymentV2TestHelper.applyPaymentV2Tx(payer, payerBalance->getBalanceID(),
                                                                 destination, paymentAmount, paymentFeeData,
                                                                 "", "", nullptr,
                                                                 PaymentV2ResultCode::MALFORMED);
        }
    }
    SECTION("Amount is less than destination fee") {
        auto opResult = paymentV2TestHelper.applyPaymentV2Tx(payer, payerBalance->getBalanceID(),
                                                             destination, 3, paymentFeeData, "", "", nullptr,
                                                             PaymentV2ResultCode::PAYMENT_AMOUNT_IS_LESS_THAN_DEST_FEE);
    }
    SECTION("Limits exceeded") {
        manageLimitsOp.details.limitsCreateDetails().dailyOut = paymentAmount - 1;
        manageLimitsTestHelper.applyManageLimitsTx(root, manageLimitsOp);
        auto opResult = paymentV2TestHelper.applyPaymentV2Tx(payer, payerBalance->getBalanceID(),
                                                             destination, paymentAmount, paymentFeeData, "", "",
                                                             nullptr, PaymentV2ResultCode::LIMITS_EXCEEDED);
    }
    SECTION("Dest fee amount overflows UINT64_MAX") {
        paymentFeeData.destinationFee.fixedFee = UINT64_MAX;
        paymentFeeData.destinationFee.maxPaymentFee = 1;
        auto opResult = paymentV2TestHelper.applyPaymentV2Tx(payer, payerBalance->getBalanceID(),
                                                             destination, paymentAmount, paymentFeeData, "", "",
                                                             nullptr, PaymentV2ResultCode::INVALID_DESTINATION_FEE);
    }
    SECTION("Payer underfunded") {
        auto opResult = paymentV2TestHelper.applyPaymentV2Tx(payer, payerBalance->getBalanceID(),
                                                             destination, paymentAmount * 4, paymentFeeData, "", "",
                                                             nullptr, PaymentV2ResultCode::UNDERFUNDED);
    }
    SECTION("Destination account not found") {
        AccountID nonExistingAccount = SecretKey::random().getPublicKey();
        auto accountDestination = paymentV2TestHelper.createDestinationForAccount(nonExistingAccount);
        auto opResult = paymentV2TestHelper.applyPaymentV2Tx(payer, payerBalance->getBalanceID(),
                                                             accountDestination, paymentAmount, paymentFeeData, "",
                                                             "", nullptr,
                                                             PaymentV2ResultCode::DESTINATION_ACCOUNT_NOT_FOUND);
    }
    SECTION("Destination balance not found") {
        BalanceID nonExistingBalance = SecretKey::random().getPublicKey();
        auto balanceDestination = paymentV2TestHelper.createDestinationForBalance(nonExistingBalance);
        auto opResult = paymentV2TestHelper.applyPaymentV2Tx(payer, payerBalance->getBalanceID(),
                                                             balanceDestination, paymentAmount, paymentFeeData, "",
                                                             "", nullptr,
                                                             PaymentV2ResultCode::DESTINATION_BALANCE_NOT_FOUND);
    }
    SECTION("Payment between different assets are not supported") {
        auto balanceDestination = paymentV2TestHelper.createDestinationForBalance(payerFeeBalance->getBalanceID());
        auto opResult = paymentV2TestHelper.applyPaymentV2Tx(payer, payerBalance->getBalanceID(),
                                                             balanceDestination, paymentAmount, paymentFeeData, "",
                                                             "", nullptr,
                                                             PaymentV2ResultCode::BALANCE_ASSETS_MISMATCHED);
    }
    SECTION("Source balance not found") {
        BalanceID nonExistingBalance = SecretKey::random().getPublicKey();
        auto opResult = paymentV2TestHelper.applyPaymentV2Tx(payer, nonExistingBalance,
                                                             destination, paymentAmount, paymentFeeData, "",
                                                             "", nullptr,
                                                             PaymentV2ResultCode::SRC_BALANCE_NOT_FOUND);
    }
    SECTION("Not allowed by asset policy") {
        manageAssetTestHelper.updateAsset(root, paymentAsset, root, static_cast<uint32_t>(AssetPolicy::BASE_ASSET));
        auto opResult = paymentV2TestHelper.applyPaymentV2Tx(payer, payerBalance->getBalanceID(),
                                                             destination, paymentAmount, paymentFeeData, "",
                                                             "", nullptr,
                                                             PaymentV2ResultCode::NOT_ALLOWED_BY_ASSET_POLICY);

        manageAssetTestHelper.updateAsset(root, paymentAsset, root, static_cast<uint32_t>(AssetPolicy::BASE_ASSET) |
                                                                    static_cast<uint32_t>(AssetPolicy::TRANSFERABLE) |
                                                                    static_cast<uint32_t>(AssetPolicy::STATS_QUOTE_ASSET) |
                                                                    static_cast<uint32_t>(AssetPolicy::REQUIRES_VERIFICATION));
        auto newPayer = Account{SecretKey::random(), Salt(1)};
        createAccountTestHelper.applyCreateAccountTx(root, newPayer.key.getPublicKey(), AccountType::NOT_VERIFIED);
        payerBalance = balanceHelper->loadBalance(newPayer.key.getPublicKey(), paymentAsset, db, nullptr);
        REQUIRE(!!payerBalance);
        opResult = paymentV2TestHelper.applyPaymentV2Tx(newPayer, payerBalance->getBalanceID(),
                                                             destination, paymentAmount, paymentFeeData, "",
                                                             "", nullptr,
                                                             PaymentV2ResultCode::NOT_ALLOWED_BY_ASSET_POLICY);

        manageAssetTestHelper.updateAsset(root, paymentAsset, root, static_cast<uint32_t>(AssetPolicy::BASE_ASSET) |
                                                                    static_cast<uint32_t>(AssetPolicy::TRANSFERABLE) |
                                                                    static_cast<uint32_t>(AssetPolicy::STATS_QUOTE_ASSET) |
                                                                    static_cast<uint32_t>(AssetPolicy::REQUIRES_KYC));
        opResult = paymentV2TestHelper.applyPaymentV2Tx(newPayer, payerBalance->getBalanceID(),
                                                        destination, paymentAmount, paymentFeeData, "",
                                                        "", nullptr,
                                                        PaymentV2ResultCode::NOT_ALLOWED_BY_ASSET_POLICY);

        newPayer = Account{SecretKey::random(), Salt(1)};
        createAccountTestHelper.applyCreateAccountTx(root, newPayer.key.getPublicKey(), AccountType::VERIFIED);
        payerBalance = balanceHelper->loadBalance(newPayer.key.getPublicKey(), paymentAsset, db, nullptr);
        REQUIRE(!!payerBalance);
        opResult = paymentV2TestHelper.applyPaymentV2Tx(newPayer, payerBalance->getBalanceID(),
                                                        destination, paymentAmount, paymentFeeData, "",
                                                        "", nullptr,
                                                        PaymentV2ResultCode::NOT_ALLOWED_BY_ASSET_POLICY);
    }
    SECTION("Destination fee asset differs from payment amount asset") {
        paymentFeeData.destinationFee.feeAsset = feeAsset;
        auto opResult = paymentV2TestHelper.applyPaymentV2Tx(payer, payerBalance->getBalanceID(),
                                                             destination, paymentAmount, paymentFeeData, "",
                                                             "", nullptr,
                                                             PaymentV2ResultCode::INVALID_DESTINATION_FEE_ASSET);
    }
    SECTION("Source fee asset mismatched") {
        issuanceTestHelper.createAssetWithPreIssuedAmount(root, "EUR", INT64_MAX, root);
        paymentFeeData.sourceFee.feeAsset = "EUR";
        auto opResult = paymentV2TestHelper.applyPaymentV2Tx(payer, payerBalance->getBalanceID(),
                                                             destination, paymentAmount, paymentFeeData, "",
                                                             "", nullptr,
                                                             PaymentV2ResultCode::FEE_ASSET_MISMATCHED);
    }
    SECTION("Insufficient fee amount") {
        paymentFeeData.sourceFee.fixedFee = static_cast<uint64>(outgoingFee.fixedFee - 1);
        paymentFeeData.sourceFee.maxPaymentFee = static_cast<uint64>(outgoingFee.percentFee - 1);
        auto opResult = paymentV2TestHelper.applyPaymentV2Tx(payer, payerBalance->getBalanceID(),
                                                             destination, paymentAmount, paymentFeeData, "",
                                                             "", nullptr,
                                                             PaymentV2ResultCode::INSUFFICIENT_FEE_AMOUNT);
    }
    SECTION("Balance to charge fee from not found") {
        auto eur = "EUR";
        issuanceTestHelper.createAssetWithPreIssuedAmount(root, eur, INT64_MAX, root);
        paymentFeeData.sourceFee.feeAsset = eur;
        manageAssetPairTestHelper.createAssetPair(root, eur, paymentAsset, 2 * ONE);
        setFeesTestHelper.applySetFeesTx(root, &outgoingFee, true);
        outgoingFee.ext.feeAsset() = eur;
        setFeesTestHelper.applySetFeesTx(root, &outgoingFee, false);
        auto opResult = paymentV2TestHelper.applyPaymentV2Tx(payer, payerBalance->getBalanceID(),
                                                             destination, paymentAmount, paymentFeeData, "",
                                                             "", nullptr,
                                                             PaymentV2ResultCode::BALANCE_TO_CHARGE_FEE_FROM_NOT_FOUND);
    }
    SECTION("Cross asset payment") {
        SECTION("Source pays for destination") {
            paymentFeeData = paymentV2TestHelper.createPaymentFeeData(sourceFeeData, destFeeData, true);

            // create paymentDelta to check balances amounts
            PaymentV2Delta paymentV2Delta;
            paymentV2Delta.source.push_back(BalanceDelta{paymentAsset, (paymentAmount + incomingFee.fixedFee) * -1});
            paymentV2Delta.source.push_back(BalanceDelta{feeAsset, (outgoingFee.fixedFee + ONE) * -1 * exchangeRatesETH_USD});
            paymentV2Delta.destination.push_back(BalanceDelta{paymentAsset, paymentAmount});
            paymentV2Delta.commission.push_back(BalanceDelta{paymentAsset, incomingFee.fixedFee});
            paymentV2Delta.commission.push_back(BalanceDelta{feeAsset, (outgoingFee.fixedFee + ONE) * exchangeRatesETH_USD});
            auto opResult = paymentV2TestHelper.applyPaymentV2Tx(payer, payerBalance->getBalanceID(), destination,
                                                                 paymentAmount, paymentFeeData, "", "",
                                                                 &paymentV2Delta);
        }
        SECTION("Destination pays") {
            paymentFeeData = paymentV2TestHelper.createPaymentFeeData(sourceFeeData, destFeeData, false);

            // create paymentDelta to check balances amounts
            PaymentV2Delta paymentV2Delta;
            paymentV2Delta.source.push_back(BalanceDelta{paymentAsset, paymentAmount * -1});
            paymentV2Delta.source.push_back(BalanceDelta{feeAsset, (outgoingFee.fixedFee + ONE) * -1 * exchangeRatesETH_USD});
            paymentV2Delta.destination.push_back(BalanceDelta{paymentAsset, paymentAmount - incomingFee.fixedFee});
            paymentV2Delta.commission.push_back(BalanceDelta{paymentAsset, incomingFee.fixedFee});
            paymentV2Delta.commission.push_back(BalanceDelta{feeAsset, (outgoingFee.fixedFee + ONE) * exchangeRatesETH_USD});
            auto opResult = paymentV2TestHelper.applyPaymentV2Tx(payer, payerBalance->getBalanceID(), destination,
                                                                 paymentAmount, paymentFeeData, "", "",
                                                                 &paymentV2Delta);
        }
    }
    SECTION("Single asset payment") {
        setFeesTestHelper.applySetFeesTx(root, &outgoingFee, true);
        outgoingFee.ext.feeAsset() = paymentAsset;
        setFeesTestHelper.applySetFeesTx(root, &outgoingFee, false);

        SECTION("Source pays for destination success") {
            paymentFeeData = paymentV2TestHelper.createPaymentFeeData(sourceFeeData, destFeeData, true);
            paymentFeeData.sourceFee.feeAsset = paymentAsset;

            // create paymentDelta to check balances amounts
            PaymentV2Delta paymentV2Delta;

            auto totalFee = outgoingFee.fixedFee + outgoingFee.percentFee + incomingFee.fixedFee +
                            incomingFee.percentFee;

            paymentV2Delta.source.push_back(BalanceDelta{paymentAsset, (paymentAmount + totalFee) * -1});
            paymentV2Delta.destination.push_back(BalanceDelta{paymentAsset, paymentAmount});
            paymentV2Delta.commission.push_back(BalanceDelta{paymentAsset, totalFee});

            auto opResult = paymentV2TestHelper.applyPaymentV2Tx(payer, payerBalance->getBalanceID(), destination,
                                                                 paymentAmount, paymentFeeData, "", "",
                                                                 &paymentV2Delta);
        }
        SECTION("Destination pays") {
            paymentFeeData = paymentV2TestHelper.createPaymentFeeData(sourceFeeData, destFeeData, false);
            paymentFeeData.sourceFee.feeAsset = paymentAsset;

            // create paymentDelta to check balances amounts
            PaymentV2Delta paymentV2Delta;

            auto totalFee = outgoingFee.fixedFee + outgoingFee.percentFee + incomingFee.fixedFee +
                            incomingFee.percentFee;

            paymentV2Delta.source.push_back(
                    BalanceDelta{paymentAsset, (paymentAmount + outgoingFee.fixedFee + outgoingFee.percentFee) * -1});

            paymentV2Delta.destination.push_back(
                    BalanceDelta{paymentAsset, paymentAmount - (incomingFee.fixedFee + incomingFee.percentFee)});

            paymentV2Delta.commission.push_back(BalanceDelta{paymentAsset, totalFee});

            auto opResult = paymentV2TestHelper.applyPaymentV2Tx(payer, payerBalance->getBalanceID(), destination,
                                                                 paymentAmount, paymentFeeData, "", "",
                                                                 &paymentV2Delta);
        }
    }

}