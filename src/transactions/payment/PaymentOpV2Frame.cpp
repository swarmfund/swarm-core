#include "main/Application.h"
#include <ledger/AccountHelper.h>
#include <ledger/AssetHelper.h>
#include <ledger/BalanceHelper.h>
#include <ledger/FeeHelper.h>
#include <ledger/ReferenceHelper.h>
#include "ledger/LedgerDelta.h"
#include <transactions/AccountManager.h>
#include "PaymentOpV2Frame.h"

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
                                                        AccountType::SYNDICATE, AccountType::EXCHANGE};

        return SourceDetails(allowedAccountTypes, mSourceAccount->getMediumThreshold(), signerType,
                             static_cast<int32_t>(BlockReasons::TOO_MANY_KYC_UPDATE_REQUESTS));
    }

    bool PaymentOpV2Frame::isRecipientFeeNotRequired() {
        return mSourceAccount->getAccountType() == AccountType::COMMISSION;
    }

    bool PaymentOpV2Frame::tryLoadDestinationBalance(Database &db, LedgerDelta &delta) {
        switch (mPayment.destination.type()) {
            case PaymentDestinationType::BALANCE: {
                mDestinationBalance = BalanceHelper::Instance()->loadBalance(mPayment.destination.balanceID(), db,
                                                                             &delta);
                if (!mDestinationBalance) {
                    innerResult().code(PaymentV2ResultCode::DESTINATION_BALANCE_NOT_FOUND);
                    return false;
                }
            }
            case PaymentDestinationType::ACCOUNT: {
                mDestinationBalance = AccountManager::loadOrCreateBalanceFrameForAsset(mPayment.destination.accountID(),
                                                                                       mSourceBalance->getAsset(), db,
                                                                                       delta);
                if (!mDestinationBalance) {
                    CLOG(ERROR, Logging::OPERATION_LOGGER)
                            << "Unexpected state: expected destination balance to exist, account id: "
                            << PubKeyUtils::toStrKey(mPayment.destination.accountID());
                    throw runtime_error("Unexpected state: expected destination balance to exist");
                }
            }
        }

        return true;
    }

    bool PaymentOpV2Frame::processBalanceChange(AccountManager::Result balanceChangeResult) {
        if (balanceChangeResult == AccountManager::Result::UNDERFUNDED) {
            innerResult().code(PaymentV2ResultCode::UNDERFUNDED);
            return false;
        }

        if (balanceChangeResult == AccountManager::Result::STATS_OVERFLOW) {
            innerResult().code(PaymentV2ResultCode::STATS_OVERFLOW);
            return false;
        }

        if (balanceChangeResult == AccountManager::Result::LIMITS_EXCEEDED) {
            innerResult().code(PaymentV2ResultCode::LIMITS_EXCEEDED);
            return false;
        }
        return true;
    }

    bool PaymentOpV2Frame::isAllowedToTransfer(Database &db) {
        if (mSourceBalance->getAsset() != mDestinationBalance->getAsset()) {
            innerResult().code(PaymentV2ResultCode::BALANCE_ASSETS_MISMATCHED);
            return false;
        }

        auto assetFrame = AssetHelper::Instance()->loadAsset(mSourceBalance->getAsset(), db);
        if (!assetFrame) {
            CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected state: expected asset to exist:"
                                                   << mSourceBalance->getAsset();
            throw std::runtime_error("Unexpected state: expected asset to exist");
        }

        if (!assetFrame->checkPolicy(AssetPolicy::TRANSFERABLE) ||
            !AccountManager::isAllowedToReceive(mDestinationBalance->getBalanceID(), db)) {
            innerResult().code(PaymentV2ResultCode::NOT_ALLOWED_BY_ASSET_POLICY);
            return false;
        }

        return true;
    }

    bool PaymentOpV2Frame::processSourceFee(AccountManager &accountManager, Database &db, LedgerDelta &delta) {
        auto sourceAccountFrame = AccountHelper::Instance()->mustLoadAccount(mSourceBalance->getAccountID(), db);
        if (!isTransferFeeMatch(sourceAccountFrame, mSourceBalance->getAsset(), mPayment.feeData.sourceFee,
                                mPayment.amount, static_cast<int64_t>(PaymentFeeType::OUTGOING), db, delta)) {
            innerResult().code(PaymentV2ResultCode::SOURCE_FEE_MISMATCHED);
            return false;
        }

        mIsCrossAssetPayment = mSourceBalance->getAsset() != mPayment.feeData.sourceFee.feeAsset;
        if (mIsCrossAssetPayment) {
            if (!tryLoadSourceFeeBalance(db, delta))
                return false;
        } else {
            mSourceFeeBalance = mSourceBalance;
        }

        uint64_t totalSourceFee = 0;
        if (!safeSum(mPayment.feeData.sourceFee.fixedFee, mActualSourcePaymentFee, totalSourceFee)) {
            CLOG(ERROR, Logging::OPERATION_LOGGER)
                    << "Unexpected state: failed to calculate total sum of source fees to be charged - overflow";
            throw std::runtime_error("Total sum of source fees to be charged - overflow");
        }

        if (totalSourceFee == 0)
            return true;

        int64_t totalSourceFeeUniversal = 0;
        auto transferResult = accountManager.processTransfer(mSourceAccount, mSourceFeeBalance, totalSourceFee,
                                                             totalSourceFeeUniversal);

        if (!processBalanceChange(transferResult))
            return false;

        mSourceSentUniversal += totalSourceFeeUniversal;
        EntryHelperProvider::storeChangeEntry(delta, db, mSourceFeeBalance->mEntry);

        return true;
    }

    bool PaymentOpV2Frame::tryLoadSourceFeeBalance(Database &db, LedgerDelta &delta) {
        auto sourceFeeAsset = mPayment.feeData.sourceFee.feeAsset;
        if (!AssetHelper::Instance()->exists(db, sourceFeeAsset)) {
            innerResult().code(PaymentV2ResultCode::SRC_FEE_ASSET_NOT_FOUND);
            return false;
        }

        mSourceFeeBalance = BalanceHelper::Instance()->loadBalance(getSourceID(), sourceFeeAsset, db, &delta);
        if (!mSourceFeeBalance) {
            innerResult().code(PaymentV2ResultCode::SRC_FEE_BALANCE_NOT_FOUND);
            return false;
        }

        return true;
    }

    bool PaymentOpV2Frame::processDestinationFee(AccountManager &accountManager, Database &db, LedgerDelta &delta) {
        if (mSourceBalance->getAsset() != mPayment.feeData.destinationFee.feeAsset) {
            innerResult().code(PaymentV2ResultCode::INVALID_DESTINATION_FEE_ASSET);
            return false;
        }

        if (isRecipientFeeNotRequired())
            return true;

        auto destinationAccountFrame = AccountHelper::Instance()->mustLoadAccount(mDestinationBalance->getAccountID(),
                                                                                  db);
        if (!isTransferFeeMatch(destinationAccountFrame, mDestinationBalance->getAsset(),
                                mPayment.feeData.destinationFee, mPayment.amount,
                                static_cast<int64_t>(PaymentFeeType::INCOMING), db, delta)) {
            innerResult().code(PaymentV2ResultCode::DESTINATION_FEE_MISMATCHED);
            return false;
        }

        uint64_t totalDestinationFee = 0;
        if (!safeSum(mPayment.feeData.destinationFee.fixedFee, mActualDestinationPaymentFee, totalDestinationFee)) {
            CLOG(ERROR, Logging::OPERATION_LOGGER)
                    << "Unexpected state: failed to calculate total sum of destination fees to be charged - overflow";
            throw std::runtime_error("Total sum of destination fees to be charged - overflow");
        }

        if (totalDestinationFee == 0)
            return true;

        int64_t totalDestinationFeeUniversal = 0;
        if (!mPayment.feeData.sourcePaysForDest) {
            auto transferResult = accountManager.processTransfer(destinationAccountFrame, mDestinationBalance,
                                                                 totalDestinationFee, totalDestinationFeeUniversal);
            if (!processBalanceChange(transferResult))
                return false;

            EntryHelperProvider::storeChangeEntry(delta, db, mDestinationBalance->mEntry);

            return true;
        }

        auto transferResult = accountManager.processTransfer(mSourceAccount, mSourceBalance,
                                                             totalDestinationFee, totalDestinationFeeUniversal);
        if (!processBalanceChange(transferResult))
            return false;

        mSourceSentUniversal += totalDestinationFeeUniversal;
        EntryHelperProvider::storeChangeEntry(delta, db, mSourceBalance->mEntry);

        return true;
    }

    //TODO: might be refactored
    bool PaymentOpV2Frame::tryFundCommissionAccount(Application &app, Database &db, LedgerDelta &delta) {
        auto commissionSourceFeeAssetBalance = AccountManager::loadOrCreateBalanceFrameForAsset(app.getCommissionID(),
                                                                                                mSourceFeeBalance->getAsset(),
                                                                                                db, delta);
        if (!commissionSourceFeeAssetBalance) {
            CLOG(ERROR, Logging::OPERATION_LOGGER)
                    << "Unexpected state: expected commission balance to exist, asset code: "
                    << mSourceBalance->getAsset();
            throw runtime_error("Unexpected state: expected commission balance to exist");
        }

        auto totalSourceFeeAssetAmount = mPayment.feeData.sourceFee.fixedFee + mActualSourcePaymentFee;
        auto totalDestinationFeeAssetAmount = mPayment.feeData.destinationFee.fixedFee + mActualDestinationPaymentFee;

        BalanceFrame::pointer commissionDestinationFeeAssetBalance = nullptr;

        if (mIsCrossAssetPayment) {
            commissionDestinationFeeAssetBalance = AccountManager::loadOrCreateBalanceFrameForAsset(
                    app.getCommissionID(),
                    mSourceBalance->getAsset(),
                    db, delta);
            if (!commissionDestinationFeeAssetBalance) {
                CLOG(ERROR, Logging::OPERATION_LOGGER)
                        << "Unexpected state: expected commission balance to exist, asset code: "
                        << mSourceFeeBalance->getAsset();
                throw runtime_error("Unexpected state: expected commission balance to exist");
            }
        }

        if (isRecipientFeeNotRequired()) {
            if (!commissionSourceFeeAssetBalance->tryFundAccount(totalSourceFeeAssetAmount)) {
                innerResult().code(PaymentV2ResultCode::LINE_FULL);
                return false;
            }
            EntryHelperProvider::storeChangeEntry(delta, db, commissionSourceFeeAssetBalance->mEntry);
            return true;
        }

        if (!commissionSourceFeeAssetBalance->tryFundAccount(totalSourceFeeAssetAmount)) {
            innerResult().code(PaymentV2ResultCode::LINE_FULL);
            return false;
        }

        if (mIsCrossAssetPayment) {
            if (!commissionDestinationFeeAssetBalance->tryFundAccount(totalDestinationFeeAssetAmount)) {
                innerResult().code(PaymentV2ResultCode::LINE_FULL);
                return false;
            }
            EntryHelperProvider::storeChangeEntry(delta, db, commissionDestinationFeeAssetBalance->mEntry);
        } else {
            if (!commissionSourceFeeAssetBalance->tryFundAccount(totalDestinationFeeAssetAmount)) {
                innerResult().code(PaymentV2ResultCode::LINE_FULL);
                return false;
            }
        }

        EntryHelperProvider::storeChangeEntry(delta, db, commissionSourceFeeAssetBalance->mEntry);
        return true;
    }

    bool PaymentOpV2Frame::isTransferFeeMatch(AccountFrame::pointer accountFrame, AssetCode const &assetCode,
                                              FeeDataV2 const &feeData, int64_t const &amount, int64_t subtype,
                                              Database &db, LedgerDelta &delta) {
        auto feeFrame = FeeHelper::Instance()->loadForAccount(FeeType::PAYMENT_FEE, assetCode, subtype, accountFrame,
                                                              amount, db);
        // if we do not have any fee frame - any fee is valid
        if (!feeFrame)
            return true;

        // TODO: maybe we should check feeFrame version before this check
        if (feeFrame->getFee().asset != assetCode || feeFrame->getFee().ext.feeAsset() != feeData.feeAsset)
            return false;

        int64_t requiredPaymentFee = feeFrame->calculatePercentFee(amount);
        int64_t requiredFixedFee = feeFrame->getFee().fixedFee;

        if (feeData.maxPaymentFee < requiredPaymentFee || feeData.fixedFee != requiredFixedFee)
            return false;

        if (subtype == static_cast<int64_t>(PaymentFeeType::OUTGOING)) {
            mActualSourcePaymentFee = static_cast<uint64_t>(requiredPaymentFee);
        } else if (subtype == static_cast<int64_t>(PaymentFeeType::INCOMING)) {
            mActualDestinationPaymentFee = static_cast<uint64_t>(requiredPaymentFee);
        }

        return true;
    }

    bool PaymentOpV2Frame::doApply(Application &app, LedgerDelta &delta, LedgerManager &ledgerManager) {
        Database &db = app.getDatabase();

        // check operation prerequisites
        mSourceBalance = BalanceHelper::Instance()->loadBalance(getSourceID(), mPayment.sourceBalanceID, db,
                                                                &delta);
        if (!mSourceBalance) {
            innerResult().code(PaymentV2ResultCode::SRC_BALANCE_NOT_FOUND);
            return false;
        }

        if (!tryLoadDestinationBalance(db, delta))
            return false;

        if (!isAllowedToTransfer(db))
            return false;

        // subtract amount from sourceBalance
        AccountManager accountManager(app, db, delta, app.getLedgerManager());

        int64_t universalAmount;
        auto transferResult = accountManager.processTransfer(mSourceAccount, mSourceBalance, mPayment.amount,
                                                             universalAmount);
        if (!processBalanceChange(transferResult))
            return false;

        mSourceSentUniversal += universalAmount;

        EntryHelperProvider::storeChangeEntry(delta, db, mSourceBalance->mEntry);

        // calculate and subtract fee from source
        if (!processSourceFee(accountManager, db, delta))
            return false;

        // try to fund destination account
        if (!mDestinationBalance->tryFundAccount(mPayment.amount)) {
            innerResult().code(PaymentV2ResultCode::LINE_FULL);
            return false;
        }

        // calculate and subtract fee from destination
        if (!processDestinationFee(accountManager, db, delta)) {
            return false;
        }

        uint64 paymentID = delta.getHeaderFrame().generateID(LedgerEntryType::PAYMENT_REQUEST);

        if (!mPayment.reference.empty()) {
            AccountID sourceAccountID = mSourceAccount->getID();

            if (ReferenceHelper::Instance()->exists(db, mPayment.reference, sourceAccountID)) {
                innerResult().code(PaymentV2ResultCode::REFERENCE_DUPLICATION);
                return false;
            }
            createReferenceEntry(mPayment.reference, &delta, db);
        }

        if (!tryFundCommissionAccount(app, db, delta)) {
            return false;
        }

        innerResult().code(PaymentV2ResultCode::SUCCESS);
        innerResult().paymentV2Response().destination = mDestinationBalance->getAccountID();
        innerResult().paymentV2Response().asset = mDestinationBalance->getAsset();
        innerResult().paymentV2Response().sourceSentUniversal = mSourceSentUniversal;
        innerResult().paymentV2Response().paymentID = paymentID;
        innerResult().paymentV2Response().actualSourcePaymentFee = mActualSourcePaymentFee;
        innerResult().paymentV2Response().actualDestinationPaymentFee = mActualDestinationPaymentFee;

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