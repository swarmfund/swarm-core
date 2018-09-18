#include "TxTests.h"
#include "crypto/SHA.h"
#include "ledger/AssetHelperLegacy.h"
#include "ledger/BalanceHelperLegacy.h"
#include "ledger/LedgerDeltaImpl.h"
#include "ledger/ReferenceFrame.h"
#include "main/Application.h"
#include "main/Config.h"
#include "main/test.h"
#include "overlay/LoopbackPeer.h"
#include "test/test_marshaler.h"
#include "test_helper/CreateAccountTestHelper.h"
#include "test_helper/IssuanceRequestHelper.h"
#include "test_helper/ManageAssetTestHelper.h"
#include "test_helper/ManageBalanceTestHelper.h"
#include "transactions/payment/PaymentOpFrame.h"
#include "util/Timer.h"

using namespace stellar;
using namespace stellar::txtest;

typedef std::unique_ptr<Application> appPtr;

TEST_CASE("payment", "[dep_tx][payment]")
{
    // TODO requires refactoring
    Config const& cfg = getTestConfig(0, Config::TESTDB_POSTGRESQL);
    VirtualClock clock;
    Application::pointer appPtr = Application::create(clock, cfg);
    Application& app = *appPtr;
    app.start();
    auto testManager = TestManager::make(app);
    TestManager::upgradeToCurrentLedgerVersion(app);
    auto root = Account{getRoot(), Salt(0)};
    auto issuanceHelper = IssuanceRequestHelper(testManager);

    int64 paymentAmount = 10 * ONE;

    Salt rootSeq = 1;

    auto emissionAmount = 2 * paymentAmount;

    const AssetCode asset = "USD";
    issuanceHelper.createAssetWithPreIssuedAmount(root, asset, INT64_MAX, root);
    ManageAssetTestHelper(testManager)
        .updateAsset(root, asset, root,
                     static_cast<uint32_t>(AssetPolicy::BASE_ASSET) |
                         static_cast<uint32_t>(AssetPolicy::TRANSFERABLE));

    // fund some account

    // aWM stends for account with money
    auto aWM = SecretKey::random();
    auto createAccountTestHelper = CreateAccountTestHelper(testManager);
    createAccountTestHelper.applyCreateAccountTx(root, aWM.getPublicKey(),
                                                 AccountType::GENERAL);
    auto aWMBalance = BalanceHelperLegacy::Instance()->loadBalance(aWM.getPublicKey(),
                                                             asset,
                                                             testManager->
                                                             getDB(), nullptr);

    uint32_t issuanceTasks = 0;

    REQUIRE(!!aWMBalance);
    issuanceHelper.applyCreateIssuanceRequest(root, asset, emissionAmount,
                                              aWMBalance->getBalanceID(),
                                              SecretKey::random().
                                              getStrKeyPublic(), &issuanceTasks);

    auto secondAsset = "AETH";

    auto balanceHelper = BalanceHelperLegacy::Instance();

    SECTION("Non base asset tests")
    {
        // create asset
        const AssetCode assetCode = "EUR";
        auto manageAssetHelper = ManageAssetTestHelper(testManager);
        manageAssetHelper.createAsset(
            root, root.key, assetCode, root,
            static_cast<uint32_t>(AssetPolicy::TRANSFERABLE));
        issuanceHelper.authorizePreIssuedAmount(root, root.key, assetCode,
                                                INT64_MAX, root);
        // create conterparties
        auto sender = SecretKey::random();
        auto receiver = SecretKey::random();
        auto balanceTestHelper = ManageBalanceTestHelper(testManager);
        for (auto counterparty : {sender, receiver})
        {
            auto pubKey = counterparty.getPublicKey();
            createAccountTestHelper.applyCreateAccountTx(root, pubKey,
                                                         AccountType::GENERAL);
            balanceTestHelper.createBalance(root, pubKey, assetCode);
        }

        // fund sender
        auto senderBalance = BalanceHelperLegacy::Instance()->loadBalance(
            sender.getPublicKey(), assetCode, testManager->getDB(), nullptr);
        REQUIRE(!!senderBalance);
        issuanceHelper.applyCreateIssuanceRequest(root, assetCode, emissionAmount,
            senderBalance->getBalanceID(),
            SecretKey::random().getStrKeyPublic(), &issuanceTasks);

        // create fee
        const int64_t fixedFee = 3;

        auto feeFrame =
            FeeFrame::create(FeeType::PAYMENT_FEE, fixedFee, 0, assetCode);
        auto fee = feeFrame->getFee();
        applySetFees(app, root.key, rootSeq++, &fee, false, nullptr);

        // perform transfer

        auto receiverBalance = BalanceHelperLegacy::Instance()->loadBalance(
            receiver.getPublicKey(), assetCode, testManager->getDB(), nullptr);
        auto paymentFee = getNoPaymentFee();
        paymentFee.sourceFee.fixedFee = fixedFee;
        paymentFee.destinationFee.fixedFee = fixedFee;
        applyPaymentTx(app, sender, senderBalance->getBalanceID(),
                       receiverBalance->getBalanceID(), 0, paymentAmount,
                       paymentFee, true, "", "");

        auto commissionBalance = balanceHelper->loadBalance(
            app.getCommissionID(), assetCode, testManager->getDB(), nullptr);
        REQUIRE(!!commissionBalance);
        REQUIRE(commissionBalance->getAmount() == fixedFee * 2);
    }
    SECTION("basic tests")
    {
        auto account = SecretKey::random();
        applyCreateAccountTx(app, root.key, account, rootSeq++,
                             AccountType::GENERAL);
        auto accountBalance = BalanceHelperLegacy::Instance()->loadBalance(
            account.getPublicKey(), asset, testManager->getDB(), nullptr);
        REQUIRE(getBalance(accountBalance->getBalanceID(), app) == 0);
        REQUIRE(getBalance(aWMBalance->getBalanceID(), app) == emissionAmount);

        auto paymentResult =
            applyPaymentTx(app, aWM, aWMBalance->getBalanceID(),
                           accountBalance->getBalanceID(), rootSeq++,
                           paymentAmount, getNoPaymentFee(), false);
        REQUIRE(getBalance(accountBalance->getBalanceID(), app) ==
                paymentAmount);
        REQUIRE(getBalance(aWMBalance->getBalanceID(), app) ==
                (emissionAmount - paymentAmount));
        REQUIRE(paymentResult.paymentResponse().destination ==
                account.getPublicKey());

        // send back
        auto accountSeq = 1;
        paymentResult =
            applyPaymentTx(app, account, accountBalance->getBalanceID(),
                           aWMBalance->getBalanceID(), accountSeq++,
                           paymentAmount, getNoPaymentFee(), false);
        REQUIRE(getBalance(accountBalance->getBalanceID(), app) == 0);
        REQUIRE(getBalance(aWMBalance->getBalanceID(), app) == emissionAmount);
        REQUIRE(paymentResult.paymentResponse().destination ==
                aWM.getPublicKey());

        auto paymentID = paymentResult.paymentResponse().paymentID;
        soci::session& sess = app.getDatabase().getSession();
    }
    SECTION("send to self")
    {
        auto balanceBefore = getAccountBalance(aWM, app);
        applyPaymentTx(app, aWM, aWM, rootSeq++, paymentAmount,
                       getNoPaymentFee(), false, "", "",
                       PaymentResultCode::MALFORMED);
    }
    SECTION("Malformed")
    {
        auto account = SecretKey::random();
        applyCreateAccountTx(app, root.key, account, rootSeq++,
                             AccountType::GENERAL);
        SECTION("Negative amount")
        {
            applyPaymentTx(app, aWM, account, rootSeq++, -100,
                           getNoPaymentFee(), false, "", "",
                           PaymentResultCode::MALFORMED);
        }
        SECTION("Zero amount")
        {
            applyPaymentTx(app, aWM, account, rootSeq++, 0, getNoPaymentFee(),
                           false, "", "", PaymentResultCode::MALFORMED);
        }
    }
    SECTION("PAYMENT_UNDERFUNDED")
    {
        auto account = SecretKey::random();
        applyCreateAccountTx(app, root.key, account, rootSeq++,
                             AccountType::GENERAL);
        auto accountBalance = BalanceHelperLegacy::Instance()->loadBalance(
            account.getPublicKey(), asset, testManager->getDB(), nullptr);
        applyPaymentTx(app, aWM, aWMBalance->getBalanceID(),
                       accountBalance->getBalanceID(), rootSeq++,
                       emissionAmount + 1, getNoPaymentFee(), false, "", "",
                       PaymentResultCode::UNDERFUNDED);
    }
    SECTION("Destination does not exist")
    {
        auto account = SecretKey::random();
        applyPaymentTx(app, aWM, aWMBalance->getBalanceID(),
                       account.getPublicKey(), rootSeq++, emissionAmount,
                       getNoPaymentFee(), false, "", "",
                       PaymentResultCode::BALANCE_NOT_FOUND);
    }
    SECTION("Payment between different assets are not supported")
    {
        auto paymentAmount = 600;
        auto account = SecretKey::random();
        auto accSeq = 1;
        auto balanceID = SecretKey::random().getPublicKey();
        std::string accountID = PubKeyUtils::toStrKey(account.getPublicKey());

        applyCreateAccountTx(app, root.key, account, rootSeq++,
                             AccountType::GENERAL);

        issuanceHelper.createAssetWithPreIssuedAmount(root, secondAsset,
                                                      emissionAmount, root);

        applyManageBalanceTx(app, account, account, accSeq++, secondAsset);

        auto accBalanceForSecondAsset = balanceHelper->loadBalance(
            account.getPublicKey(), secondAsset, app.getDatabase(), nullptr);

        auto paymentResult =
            applyPaymentTx(app, aWM, aWMBalance->getBalanceID(),
                           accBalanceForSecondAsset->getBalanceID(), rootSeq++,
                           paymentAmount, getNoPaymentFee(), false, "", "",
                           PaymentResultCode::BALANCE_ASSETS_MISMATCHED);
    }
    SECTION("Payment fee")
    {
        int64 feeAmount = 2 * ONE; // fee is 2%
        int64_t fixedFee = 3;

        auto feeFrame =
            FeeFrame::create(FeeType::PAYMENT_FEE, fixedFee, feeAmount, asset);
        auto fee = feeFrame->getFee();

        applySetFees(app, root.key, rootSeq++, &fee, false, nullptr);
        auto account = SecretKey::random();
        applyCreateAccountTx(app, root.key, account, rootSeq++,
                             AccountType::GENERAL);
        auto accountSeq = 1;
        int64 balance = 600 * ONE;
        paymentAmount = 6 * ONE;
        PaymentFeeData paymentFee = getGeneralPaymentFee(
            fixedFee, paymentAmount * (feeAmount / ONE) / 100);
        auto accountBalance = BalanceHelperLegacy::Instance()->loadBalance(
            account.getPublicKey(), asset, testManager->getDB(), nullptr);
        REQUIRE(!!accountBalance);
        issuanceHelper.applyCreateIssuanceRequest(root, asset, balance,
                                                  accountBalance->
                                                  getBalanceID(),
                                                  SecretKey::random().
                                                  getStrKeyPublic(), &issuanceTasks);
        auto dest = SecretKey::random();
        applyCreateAccountTx(app, root.key, dest, rootSeq++,
                             AccountType::GENERAL);
        auto destBalance = BalanceHelperLegacy::Instance()->loadBalance(
            dest.getPublicKey(), asset, testManager->getDB(), nullptr);
        ;
        REQUIRE(!!destBalance);
        SECTION("Fee mismatched")
        {
            auto invalidFee = paymentFee;
            invalidFee.sourceFee.paymentFee -= 1;
            applyPaymentTx(app, account, accountBalance->getBalanceID(),
                           destBalance->getBalanceID(), accountSeq++,
                           paymentAmount, invalidFee, false, "", "",
                           PaymentResultCode::FEE_MISMATCHED);
            invalidFee = paymentFee;
            invalidFee.sourceFee.fixedFee -= 1;
            applyPaymentTx(app, account, accountBalance->getBalanceID(),
                           destBalance->getBalanceID(), accountSeq++,
                           paymentAmount, invalidFee, false, "", "",
                           PaymentResultCode::FEE_MISMATCHED);
        }
        auto commission = getCommissionKP();
        auto comissionBalance = BalanceHelperLegacy::Instance()->loadBalance(
            commission.getPublicKey(), asset, testManager->getDB(), nullptr);
        uint64 totalFee = 2 * (paymentFee.sourceFee.paymentFee +
                               paymentFee.sourceFee.fixedFee);
        SECTION("Success source is paying")
        {
            paymentFee.sourcePaysForDest = true;
            applyPaymentTx(app, account, accountBalance->getBalanceID(),
                           destBalance->getBalanceID(), accountSeq++,
                           paymentAmount, paymentFee, true);
            accountBalance = BalanceHelperLegacy::Instance()->loadBalance(
                account.getPublicKey(), asset, testManager->getDB(), nullptr);
            REQUIRE(getBalance(accountBalance->getBalanceID(), app) ==
                    balance - paymentAmount - totalFee);
            REQUIRE(getBalance(destBalance->getBalanceID(), app) ==
                    paymentAmount);
            REQUIRE(getBalance(comissionBalance->getBalanceID(), app) ==
                    totalFee);
        }
        SECTION("Success dest is paying")
        {
            paymentFee.sourcePaysForDest = false;
            applyPaymentTx(app, account, accountBalance->getBalanceID(),
                           destBalance->getBalanceID(), accountSeq++,
                           paymentAmount, paymentFee, false);
            accountBalance = BalanceHelperLegacy::Instance()->loadBalance(
                account.getPublicKey(), asset, testManager->getDB(), nullptr);
            REQUIRE(getBalance(accountBalance->getBalanceID(), app) ==
                    balance - paymentAmount - totalFee / 2);
            REQUIRE(getBalance(destBalance->getBalanceID(), app) ==
                    paymentAmount - totalFee / 2);
            REQUIRE(getBalance(comissionBalance->getBalanceID(), app) ==
                    totalFee);
            SECTION("Recipient fee is not required")
            {
                auto payment =
                    createPaymentTx(app.getNetworkID(), commission,
                                    comissionBalance->getBalanceID(),
                                    destBalance->getBalanceID(), 0, totalFee,
                                    getNoPaymentFee(), false);
                payment->getEnvelope().signatures.clear();
                payment->addSignature(root.key);
                LedgerDeltaImpl delta(
                    app.getLedgerManager().getCurrentLedgerHeader(),
                    app.getDatabase());
                REQUIRE(applyCheck(payment, delta, app));
                REQUIRE(PaymentOpFrame::getInnerCode(getFirstResult(
                            *payment)) == PaymentResultCode::SUCCESS);
            }
        }
    }
    SECTION("Payment fee with minimum values")
    {
        int64 feeAmount = 1;
        auto feeFrame =
            FeeFrame::create(FeeType::PAYMENT_FEE, 0, feeAmount, asset);
        auto fee = feeFrame->getFee();

        applySetFees(app, root.key, rootSeq++, &fee, false, nullptr);
        auto account = SecretKey::random();
        applyCreateAccountTx(app, root.key, account, rootSeq++,
                             AccountType::GENERAL);
        auto accountSeq = 1;
        int64 balance = 60 * ONE;
        paymentAmount = 1;
        PaymentFeeData paymentFee = getNoPaymentFee();
        paymentFee.sourcePaysForDest = true;

        auto accountBalance = BalanceHelperLegacy::Instance()->loadBalance(
            account.getPublicKey(), asset, testManager->getDB(), nullptr);
        REQUIRE(!!accountBalance);
        issuanceHelper.applyCreateIssuanceRequest(root, asset, balance,
                                                  accountBalance->
                                                  getBalanceID(),
                                                  SecretKey::random().
                                                  getStrKeyPublic(), &issuanceTasks);

        auto dest = SecretKey::random();
        applyCreateAccountTx(app, root.key, dest, rootSeq++,
                             AccountType::GENERAL);
        auto destBalance = BalanceHelperLegacy::Instance()->loadBalance(
            dest.getPublicKey(), asset, testManager->getDB(), nullptr);
        ;
        REQUIRE(!!destBalance);

        applyPaymentTx(app, account, accountBalance->getBalanceID(),
                       destBalance->getBalanceID(), accountSeq++, paymentAmount,
                       paymentFee, true, "", "",
                       PaymentResultCode::FEE_MISMATCHED);
        paymentFee = getGeneralPaymentFee(0, 1);

        applyPaymentTx(app, account, accountBalance->getBalanceID(),
                       destBalance->getBalanceID(), accountSeq++, paymentAmount,
                       paymentFee, true, "");
    }
}
