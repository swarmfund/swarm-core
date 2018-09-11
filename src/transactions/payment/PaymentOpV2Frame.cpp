#include "PaymentOpV2Frame.h"
#include "ledger/LedgerDelta.h"
#include "ledger/StorageHelper.h"
#include "main/Application.h"
#include <ledger/AccountHelper.h>
#include <ledger/AssetHelper.h>
#include <ledger/AssetPairHelper.h>
#include <ledger/BalanceHelper.h>
#include <ledger/FeeHelper.h>
#include <ledger/LedgerHeaderFrame.h>
#include <ledger/ReferenceHelper.h>
#include <transactions/AccountManager.h>

namespace stellar {
    using namespace std;
    using xdr::operator==;

    PaymentOpV2Frame::PaymentOpV2Frame(const stellar::Operation &op, stellar::OperationResult &res,
                                       stellar::TransactionFrame &parentTx)
            : OperationFrame(op, res, parentTx), mPayment(mOperation.body.paymentOpV2()) {

    }

    std::unordered_map<AccountID, CounterpartyDetails>
    PaymentOpV2Frame::getCounterpartyDetails(Database &db, LedgerDelta *delta) const {
        // there are no restrictions for counterparty to receive payment on current stage,
        // so no need to load it.
        return {};
    }

    SourceDetails
    PaymentOpV2Frame::getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
                                              int32_t ledgerVersion) const {
        int32_t signerType = static_cast<int32_t >(SignerType::BALANCE_MANAGER);
        switch (mSourceAccount->getAccountType()) {
            case AccountType::OPERATIONAL:
                signerType = static_cast<int32_t >(SignerType::OPERATIONAL_BALANCE_MANAGER);
                break;
            case AccountType::COMMISSION:
                signerType = static_cast<int32_t >(SignerType::COMMISSION_BALANCE_MANAGER);
                break;
            default:
                break;
        }

        std::vector<AccountType> allowedAccountTypes = {AccountType::NOT_VERIFIED, AccountType::GENERAL,
                                                        AccountType::OPERATIONAL, AccountType::COMMISSION,
                                                        AccountType::SYNDICATE, AccountType::EXCHANGE,
                                                        AccountType::ACCREDITED_INVESTOR,
                                                        AccountType::INSTITUTIONAL_INVESTOR,
                                                        AccountType::VERIFIED};

        return SourceDetails(allowedAccountTypes, mSourceAccount->getMediumThreshold(), signerType,
                             static_cast<int32_t>(BlockReasons::TOO_MANY_KYC_UPDATE_REQUESTS) |
                             static_cast<uint32_t>(BlockReasons::WITHDRAWAL));
    }

    bool PaymentOpV2Frame::processTransfer(AccountManager &accountManager, AccountFrame::pointer payer,
                                           BalanceFrame::pointer from, BalanceFrame::pointer to, uint64_t amount,
                                           uint64_t &universalAmount, Database &db) {
        auto transferResult = accountManager.processTransferV2(payer, from, to, amount);
        if (transferResult.result != AccountManager::Result::SUCCESS) {
            setErrorCode(transferResult.result);
            return false;
        }

        if (!safeSum(universalAmount, transferResult.universalAmount, universalAmount)) {
            innerResult().code(PaymentV2ResultCode::STATS_OVERFLOW);
            return false;
        }

        return true;
    }

    bool PaymentOpV2Frame::processTransferFee(AccountManager &accountManager, AccountFrame::pointer payer,
                                              BalanceFrame::pointer chargeFrom, FeeDataV2 expectedFee,
                                              FeeDataV2 actualFee, AccountID const &commissionID,
                                              Database &db, LedgerDelta &delta, bool ignoreStats,
                                              uint64_t &universalAmount) {
        if (actualFee.fixedFee == 0 && actualFee.maxPaymentFee == 0) {
            return true;
        }

        if (actualFee.feeAsset != expectedFee.feeAsset) {
            innerResult().code(PaymentV2ResultCode::FEE_ASSET_MISMATCHED);
            return false;
        }

        if (expectedFee.fixedFee < actualFee.fixedFee || expectedFee.maxPaymentFee < actualFee.maxPaymentFee) {
            innerResult().code(PaymentV2ResultCode::INSUFFICIENT_FEE_AMOUNT);
            return false;
        }

        uint64_t totalFee;
        if (!safeSum(actualFee.fixedFee, actualFee.maxPaymentFee, totalFee)) {
            CLOG(ERROR, Logging::OPERATION_LOGGER)
                    << "Unexpected state: failed to calculate total sum of fees to be charged - overflow";
            throw std::runtime_error("Total sum of fees to be charged overflows");
        }

        // try to load balance for fee to be charged
        if (chargeFrom->getAsset() != actualFee.feeAsset) {
            chargeFrom = BalanceHelper::Instance()->loadBalance(chargeFrom->getAccountID(), actualFee.feeAsset, db,
                                                                &delta);
            if (!chargeFrom) {
                innerResult().code(PaymentV2ResultCode::BALANCE_TO_CHARGE_FEE_FROM_NOT_FOUND);
                return false;
            }
        }

        // load commission balance
        auto commissionBalance = AccountManager::loadOrCreateBalanceFrameForAsset(commissionID, actualFee.feeAsset, db,
                                                                                  delta);
        if (!commissionBalance) {
            CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected state. Expected commission balance to exist";
            throw std::runtime_error("Unexpected state. Expected commission balance to exist");
        }

        return processTransfer(accountManager, payer, chargeFrom, commissionBalance, totalFee, universalAmount, db);
    }

    void PaymentOpV2Frame::setErrorCode(AccountManager::Result transferResult) {
        switch (transferResult) {
            case AccountManager::Result::UNDERFUNDED: {
                innerResult().code(PaymentV2ResultCode::UNDERFUNDED);
                return;
            }
            case AccountManager::Result::STATS_OVERFLOW: {
                innerResult().code(PaymentV2ResultCode::STATS_OVERFLOW);
                return;
            }
            case AccountManager::Result::LIMITS_EXCEEDED: {
                innerResult().code(PaymentV2ResultCode::LIMITS_EXCEEDED);
                return;
            }
            case AccountManager::Result::LINE_FULL: {
                innerResult().code(PaymentV2ResultCode::LINE_FULL);
                return;
            }
            default: {
                CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected result code from process transfer v2: "
                                                       << transferResult;
                throw std::runtime_error("Unexpected result code from process transfer v2");
            }
        }
    }

    bool PaymentOpV2Frame::isRecipientFeeNotRequired() {
        return mSourceAccount->getAccountType() == AccountType::COMMISSION;
    }

    BalanceFrame::pointer
    PaymentOpV2Frame::tryLoadDestinationBalance(AssetCode asset, Database &db, LedgerDelta &delta, LedgerManager& lm) {
        switch (mPayment.destination.type()) {
            case PaymentDestinationType::BALANCE: {
                auto dest = BalanceHelper::Instance()->loadBalance(mPayment.destination.balanceID(), db,
                                                                   &delta);
                if (!dest) {
                    innerResult().code(PaymentV2ResultCode::DESTINATION_BALANCE_NOT_FOUND);
                    return nullptr;
                }

                if (dest->getAsset() != asset) {
                    innerResult().code(PaymentV2ResultCode::BALANCE_ASSETS_MISMATCHED);
                    return nullptr;
                }

                return dest;
            }
            case PaymentDestinationType::ACCOUNT: {
                if (lm.shouldUse(LedgerVersion::FIX_PAYMENT_V2_DEST_ACCOUNT_NOT_FOUND) &&
                    !AccountHelper::Instance()->exists(mPayment.destination.accountID(), db)) {
                    innerResult().code(PaymentV2ResultCode::DESTINATION_ACCOUNT_NOT_FOUND);
                    return nullptr;
                }

                auto dest = AccountManager::loadOrCreateBalanceFrameForAsset(mPayment.destination.accountID(),
                                                                             asset, db,
                                                                             delta);
                if (!dest) {
                    CLOG(ERROR, Logging::OPERATION_LOGGER)
                            << "Unexpected state: expected destination balance to exist, account id: "
                            << PubKeyUtils::toStrKey(mPayment.destination.accountID());
                    throw runtime_error("Unexpected state: expected destination balance to exist");
                }

                return dest;
            }
            default:
                throw std::runtime_error("Unexpected destination type on payment v2");
        }
    }

    bool PaymentOpV2Frame::isTransferAllowed(BalanceFrame::pointer from, BalanceFrame::pointer to, Database &db) {
        if (from->getAsset() != to->getAsset()) {
            innerResult().code(PaymentV2ResultCode::BALANCE_ASSETS_MISMATCHED);
            return false;
        }

        // is transfer allowed by asset policy
        auto asset = AssetHelper::Instance()->mustLoadAsset(from->getAsset(), db);
        if (!asset->isPolicySet(AssetPolicy::TRANSFERABLE)) {
            innerResult().code(PaymentV2ResultCode::NOT_ALLOWED_BY_ASSET_POLICY);
            return false;
        }

        // is holding asset require KYC or VERIFICATION
        if (!asset->isRequireKYC() && !asset->isRequireVerification()) {
            return true;
        }

        auto &sourceAccount = getSourceAccount();
        auto destAccount = AccountHelper::Instance()->mustLoadAccount(to->getAccountID(), db);

        if ((sourceAccount.getAccountType() == AccountType::NOT_VERIFIED ||
            destAccount->getAccountType() == AccountType::NOT_VERIFIED) && asset->isRequireVerification()) {
            innerResult().code(PaymentV2ResultCode::NOT_ALLOWED_BY_ASSET_POLICY);
            return false;
        }

        if (sourceAccount.getAccountType() == AccountType::NOT_VERIFIED ||
            sourceAccount.getAccountType() == AccountType::VERIFIED ||
            destAccount->getAccountType() == AccountType::NOT_VERIFIED ||
            destAccount->getAccountType() == AccountType::VERIFIED) {
            innerResult().code(PaymentV2ResultCode::NOT_ALLOWED_BY_ASSET_POLICY);
            return false;
        }

        return true;
    }

    FeeDataV2
    PaymentOpV2Frame::getActualFee(AccountFrame::pointer accountFrame, AssetCode const &transferAsset, uint64_t amount,
                                   PaymentFeeType feeType, Database &db, LedgerManager& lm) {
        FeeDataV2 actualFee;
        actualFee.feeAsset = transferAsset;
        actualFee.maxPaymentFee = 0;
        actualFee.fixedFee = 0;
        auto feeFrame = FeeHelper::Instance()->loadForAccount(FeeType::PAYMENT_FEE, transferAsset,
                                                              static_cast<int64_t>(feeType),
                                                              accountFrame, amount, db);
        // if we do not have any fee frame - any fee is valid
        if (!feeFrame) {
            return actualFee;
        }

        actualFee.feeAsset = feeFrame->getFeeAsset();
        if (actualFee.feeAsset != transferAsset) {
            auto assetPair = AssetPairHelper::Instance()->tryLoadAssetPairForAssets(actualFee.feeAsset, transferAsset,
                                                                                    db);
            if (!assetPair) {
                CLOG(ERROR, Logging::OPERATION_LOGGER)
                        << "Unexpected state. Failed to load asset pair for cross asset fee: "
                        << actualFee.feeAsset << " " << transferAsset;
                throw std::runtime_error("Unexpected state. Failed to load asset pair for cross asset fee");
            }

            AssetCode destAsset;
            if (lm.shouldUse(LedgerVersion::FIX_PAYMENT_V2_FEE))
                destAsset = actualFee.feeAsset;
            else
                destAsset = transferAsset;

            if (!assetPair->convertAmount(destAsset, amount, Rounding::ROUND_UP, amount)) {
                // most probably it will not happen, but it'd better to return error code
                throw std::runtime_error("failed to convert transfer amount into fee asset");
            }
        }

        if (!feeFrame->calculatePercentFee(amount, actualFee.maxPaymentFee, ROUND_UP)) {
            CLOG(ERROR, Logging::OPERATION_LOGGER) << "Failed to calculate actual payment fee - overflow, asset code: "
                                                   << feeFrame->getFeeAsset();
            throw std::runtime_error("Failed to calculate actual payment fee - overflow");
        }

        actualFee.fixedFee = static_cast<uint64>(feeFrame->getFee().fixedFee);
        return actualFee;
    }

    bool
    PaymentOpV2Frame::isSendToSelf(LedgerManager& lm, BalanceID sourceBalanceID, BalanceID destBalanceID)
    {
        if (!lm.shouldUse(LedgerVersion::FIX_PAYMENT_V2_SEND_TO_SELF))
        {
            return false;
        }

        return sourceBalanceID == destBalanceID;
    }

    bool
    PaymentOpV2Frame::doApply(Application& app, StorageHelper& storageHelper,
                            LedgerManager& ledgerManager)
    {
        Database& db = storageHelper.getDatabase();
        LedgerDelta& delta = storageHelper.getLedgerDelta();
        auto sourceBalance = BalanceHelper::Instance()->loadBalance(
            getSourceID(), mPayment.sourceBalanceID, db, &delta);
        if (!sourceBalance)
        {
            innerResult().code(PaymentV2ResultCode::SRC_BALANCE_NOT_FOUND);
            return false;
        }

        auto destBalance = tryLoadDestinationBalance(sourceBalance->getAsset(), db, delta, ledgerManager);
        if (!destBalance) {
            return false;
        }

        BalanceID destBalanceID = destBalance->getBalanceID();

        if (isSendToSelf(ledgerManager, mPayment.sourceBalanceID, destBalanceID))
        {
            innerResult().code(PaymentV2ResultCode::MALFORMED);
            return false;
        }

        if (!isTransferAllowed(sourceBalance, destBalance, db)) {
            return false;
        }

        uint64_t sourceSentUniversal = 0;

        AccountManager accountManager(app, db, delta, app.getLedgerManager());
        // process transfer from source to dest
        auto sourceAccount = AccountHelper::Instance()->mustLoadAccount(getSourceID(), db);

        if (!processTransfer(accountManager, sourceAccount, sourceBalance, destBalance, mPayment.amount, sourceSentUniversal, db)) {
            return false;
        }

        auto sourceFee = getActualFee(sourceAccount, sourceBalance->getAsset(), mPayment.amount,
                                      PaymentFeeType::OUTGOING, db, ledgerManager);

        if (!processTransferFee(accountManager, sourceAccount, sourceBalance, mPayment.feeData.sourceFee, sourceFee,
                                app.getCommissionID(), db, delta, false, sourceSentUniversal)) {
            return false;
        }

        auto destAccount = AccountHelper::Instance()->mustLoadAccount(destBalance->getAccountID(), db);
        auto destFee = getActualFee(destAccount, sourceBalance->getAsset(), mPayment.amount, PaymentFeeType::INCOMING,
                                    db, ledgerManager);

        // destination fee asset must be the same as asset of payment
        // cross asset fee is not allowed for payment destination balance
        if (destFee.feeAsset != sourceBalance->getAsset() ||
            mPayment.feeData.destinationFee.feeAsset != sourceBalance->getAsset()) {
            innerResult().code(PaymentV2ResultCode::INVALID_DESTINATION_FEE_ASSET);
            return false;
        }

        if (!isRecipientFeeNotRequired()) {
            auto destFeePayer = destAccount;
            auto destFeePayerBalance = destBalance;
            if (mPayment.feeData.sourcePaysForDest) {
                destFeePayer = sourceAccount;
                destFeePayerBalance = sourceBalance;
            }

            uint64_t destFeeUniversalAmount = 0;

            if (!processTransferFee(accountManager, destFeePayer, destFeePayerBalance, mPayment.feeData.destinationFee,
                                    destFee, app.getCommissionID(), db, delta, true, destFeeUniversalAmount)) {
                return false;
            }

            if (mPayment.feeData.sourcePaysForDest)
                sourceSentUniversal += destFeeUniversalAmount;
        }

        uint64 paymentID = delta.getHeaderFrame().generateID(LedgerEntryType::PAYMENT_REQUEST);

        if (!mPayment.reference.empty()) {
            AccountID sourceAccountID = mSourceAccount->getID();

            if (ReferenceHelper::Instance()->exists(db, mPayment.reference, sourceAccountID)) {
                innerResult().code(PaymentV2ResultCode::REFERENCE_DUPLICATION);
                return false;
            }
        createReferenceEntry(mPayment.reference, storageHelper);
        }

        innerResult().code(PaymentV2ResultCode::SUCCESS);
        innerResult().paymentV2Response().destination = destAccount->getID();
        innerResult().paymentV2Response().destinationBalanceID = destBalance->getBalanceID();
        innerResult().paymentV2Response().asset = destBalance->getAsset();
        innerResult().paymentV2Response().sourceSentUniversal = sourceSentUniversal;
        innerResult().paymentV2Response().paymentID = paymentID;
        innerResult().paymentV2Response().actualSourcePaymentFee = sourceFee.maxPaymentFee;
        innerResult().paymentV2Response().actualDestinationPaymentFee = destFee.maxPaymentFee;

        return true;
    }

    bool PaymentOpV2Frame::isDestinationFeeValid() {
        uint64_t totalDestinationFee;
        if (!safeSum(mPayment.feeData.destinationFee.fixedFee, mPayment.feeData.destinationFee.maxPaymentFee,
                     totalDestinationFee)) {
            innerResult().code(PaymentV2ResultCode::INVALID_DESTINATION_FEE);
            return false;
        }

        if (mPayment.feeData.sourcePaysForDest)
            return true;

        if (mPayment.amount < totalDestinationFee) {
            innerResult().code(PaymentV2ResultCode::PAYMENT_AMOUNT_IS_LESS_THAN_DEST_FEE);
            return false;
        }

        return true;
    }

    bool PaymentOpV2Frame::doCheckValid(Application &app) {
        if (!AssetFrame::isAssetCodeValid(mPayment.feeData.sourceFee.feeAsset) ||
            !AssetFrame::isAssetCodeValid(mPayment.feeData.destinationFee.feeAsset)) {
            innerResult().code(PaymentV2ResultCode::MALFORMED);
            return false;
        }

        if (mPayment.reference.length() > 64) {
            innerResult().code(PaymentV2ResultCode::MALFORMED);
            return false;
        }

        if (!isDestinationFeeValid()) {
            return false;
        }

        if (mPayment.destination.type() != PaymentDestinationType::BALANCE) {
            return true;
        }

        if (mPayment.sourceBalanceID == mPayment.destination.balanceID()) {
            innerResult().code(PaymentV2ResultCode::MALFORMED);
            return false;
        }

        return true;
    }
}