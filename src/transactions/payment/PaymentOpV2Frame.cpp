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
            : OperationFrame(op, res, parentTx), mPaymentV2(mOperation.body.paymentOpV2()) {}

    std::unordered_map<AccountID, CounterpartyDetails>
    PaymentOpV2Frame::getCounterpartyDetails(Database &db, LedgerDelta *delta) const {
        if (mPaymentV2.destination.type() == PaymentDestinationType::ACCOUNT) {
            return {
                    {mPaymentV2.destination.accountID(), CounterpartyDetails(
                            {AccountType::NOT_VERIFIED, AccountType::GENERAL, AccountType::OPERATIONAL,
                             AccountType::COMMISSION, AccountType::SYNDICATE, AccountType::EXCHANGE},
                            true, false)}
            };
        }

        if (mPaymentV2.destination.type() == PaymentDestinationType::BALANCE) {
            auto targetBalance = BalanceHelper::Instance()->loadBalance(mPaymentV2.destination.balanceID(), db, delta);
            // counterparty does not exist, error will be returned from do apply
            if (!targetBalance)
                return {};
            return {
                    {targetBalance->getAccountID(), CounterpartyDetails(
                            {AccountType::NOT_VERIFIED, AccountType::GENERAL, AccountType::OPERATIONAL,
                             AccountType::COMMISSION, AccountType::SYNDICATE, AccountType::EXCHANGE},
                            true, false)}
            };
        }
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
        std::vector<AccountType> allowedAccountTypes;
        if (ledgerVersion >= int32_t(LedgerVersion::USE_KYC_LEVEL)) {
            allowedAccountTypes = {AccountType::NOT_VERIFIED, AccountType::GENERAL, AccountType::OPERATIONAL,
                                   AccountType::COMMISSION, AccountType::SYNDICATE, AccountType::EXCHANGE};
        }
        return SourceDetails(allowedAccountTypes, mSourceAccount->getMediumThreshold(), signerType,
                             static_cast<int32_t>(BlockReasons::TOO_MANY_KYC_UPDATE_REQUESTS));
    }

    bool
    PaymentOpV2Frame::tryLoadBalances(stellar::Application &app, stellar::Database &db, stellar::LedgerDelta &delta) {
        auto balanceHelper = BalanceHelper::Instance();

        mSourceBalance = balanceHelper->loadBalance(mPaymentV2.sourceBalanceID, db, &delta);
        if (!mSourceBalance) {
            innerResult().code(PaymentV2ResultCode::SRC_BALANCE_NOT_FOUND);
            return false;
        }

        if (!(mSourceBalance->getAccountID() == getSourceID())) {
            innerResult().code(PaymentV2ResultCode::BALANCE_ACCOUNT_MISMATCHED);
            return false;
        }

        if (mPaymentV2.destination.type() == PaymentDestinationType::BALANCE) {
            mDestBalance = balanceHelper->loadBalance(mPaymentV2.destination.balanceID(), db, &delta);
            if (!mDestBalance) {
                innerResult().code(PaymentV2ResultCode::DESTINATION_BALANCE_NOT_FOUND);
                return false;
            }
        }

        if (mPaymentV2.destination.type() == PaymentDestinationType::ACCOUNT) {
            mDestBalance = AccountManager::loadOrCreateBalanceFrameForAsset(mPaymentV2.destination.accountID(),
                                                                            mSourceBalance->getAsset(), db, delta);
            if (!mDestBalance) {
                CLOG(ERROR, Logging::OPERATION_LOGGER)
                        << "Unexpected state: expected destination balance to exist, account id: "
                        << PubKeyUtils::toStrKey(mPaymentV2.destination.accountID());
                throw runtime_error("Unexpected state: expected destination balance to exist");
            }
        }

        if (!(mSourceBalance->getAsset() == mDestBalance->getAsset())) {
            innerResult().code(PaymentV2ResultCode::BALANCE_ASSETS_MISMATCHED);
            return false;
        }

        auto accountHelper = AccountHelper::Instance();
        mDestAccount = accountHelper->loadAccount(mDestBalance->getAccountID(), db);
        if (!mDestAccount)
            throw std::runtime_error("Invalid database state - can't load account for balance");

        return true;
    }

    bool PaymentOpV2Frame::checkFees(Application &app, Database &db, LedgerDelta &delta) {
        bool areFeesNotRequired = isSystemAccountType(mSourceAccount->getAccountType());
        if (areFeesNotRequired)
            return true;

        AssetCode asset = mSourceBalance->getAsset();
        if (!isTransferFeeMatch(mSourceAccount, asset, mPaymentV2.feeData.sourceFee, mPaymentV2.amount,
                                PaymentFeeType::OUTGOING, db, delta)) {
            innerResult().code(PaymentV2ResultCode::FEE_MISMATCHED);
            return false;
        }

        if (isRecipientFeeNotRequired(db)) {
            return true;
        }

        if (!isTransferFeeMatch(mDestAccount, asset, mPaymentV2.feeData.destinationFee, mPaymentV2.amount,
                                PaymentFeeType::INCOMING, db, delta)) {
            innerResult().code(PaymentV2ResultCode::FEE_MISMATCHED);
            return false;
        }

        return true;
    }

    bool PaymentOpV2Frame::isRecipientFeeNotRequired(Database &db) {
        return mSourceAccount->getAccountType() == AccountType::COMMISSION;
    }

    bool PaymentOpV2Frame::isTransferFeeMatch(AccountFrame::pointer accountFrame, AssetCode const &assetCode,
                                              FeeDataV2 const &feeData, uint64_t const &amount,
                                              PaymentFeeType paymentFeeType, Database &db, LedgerDelta &delta) {
        auto feeHelper = FeeHelper::Instance();

        auto feeFrame = feeHelper->loadForAccount(FeeType::PAYMENT_FEE, assetCode, static_cast<int64_t>(paymentFeeType),
                                                  accountFrame, amount, db,
                                                  &delta);
        if (!feeFrame)
            return true;

        uint64_t requiredPaymentFee = 0;
        feeFrame->calculatePercentFee(amount, requiredPaymentFee, Rounding::ROUND_UP);

        int64 requiredFixedFee = feeFrame->getFee().fixedFee;

        return feeData.paymentFee == requiredPaymentFee && feeData.fixedFee == requiredFixedFee;
    }

    bool PaymentOpV2Frame::isAllowedToTransfer(Database &db, AssetFrame::pointer asset) {
        return asset->checkPolicy(AssetPolicy::TRANSFERABLE);
    }

    bool PaymentOpV2Frame::processFees(Application &app, LedgerDelta &delta, Database &db) {
        uint64_t totalFees;
        if (!safeSum(totalFees, std::vector<uint64_t>{uint64_t(mPaymentV2.feeData.sourceFee.fixedFee),
                                                      uint64_t(mPaymentV2.feeData.sourceFee.paymentFee),
                                                      uint64_t(mPaymentV2.feeData.destinationFee.fixedFee),
                                                      uint64_t(mPaymentV2.feeData.destinationFee.paymentFee)})) {
            CLOG(ERROR, Logging::OPERATION_LOGGER)
                    << "Unexpected state: failed to calculate total sum of fees to be charged - overflow";
            throw std::runtime_error("Total sum of fees to be charged - overlows");
        }

        if (totalFees == 0) {
            return true;
        }

        auto balance = AccountManager::loadOrCreateBalanceFrameForAsset(app.getCommissionID(),
                                                                        mSourceBalance->getAsset(), db, delta);
        if (!balance->tryFundAccount(totalFees)) {
            innerResult().code(PaymentV2ResultCode::LINE_FULL);
            return false;
        }

        EntryHelperProvider::storeChangeEntry(delta, db, balance->mEntry);
        return true;
    }

    bool PaymentOpV2Frame::processBalanceChange(Application &app, AccountManager::Result balanceChangeResult) {
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

    bool PaymentOpV2Frame::doApply(Application &app, LedgerDelta &delta, LedgerManager &ledgerManager) {
        innerResult().code(PaymentV2ResultCode::SUCCESS);

        Database &db = ledgerManager.getDatabase();

        if (!tryLoadBalances(app, db, delta)) {
            // failed to load balances
            return false;
        }

        auto assetHelper = AssetHelper::Instance();

        auto assetFrame = assetHelper->loadAsset(mSourceBalance->getAsset(), db);
        if (!assetFrame) {
            CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected state: expected asset to exist:"
                                                   << mSourceBalance->getAsset();
            throw std::runtime_error("Unexpected state: expected asset to exist");
        }

        if (mSourceBalance->getAsset() != mPaymentV2.feeData.feeAsset) {
            if (!assetHelper->exists(db, mPaymentV2.feeData.feeAsset)) {
                CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected state: expected fee asset to exist:"
                                                       << mPaymentV2.feeData.feeAsset;
                throw std::runtime_error("Unexpected state: expected fee asset to exist");
            }
        }

        if (!isAllowedToTransfer(db, assetFrame) ||
            !AccountManager::isAllowedToReceive(mDestBalance->getBalanceID(), db)) {
            innerResult().code(PaymentV2ResultCode::NOT_ALLOWED_BY_ASSET_POLICY);
            return false;
        }

        if (!checkFees(app, db, delta)) {
            return false;
        }

        AccountManager accountManager(app, db, delta, ledgerManager);

        uint64 paymentID = delta.getHeaderFrame().generateID(LedgerEntryType::PAYMENT_REQUEST);

        if (!mPaymentV2.reference.empty()) {
            AccountID sourceAccountID = mSourceAccount->getID();

            auto referenceHelper = ReferenceHelper::Instance();
            if (referenceHelper->exists(db, mPaymentV2.reference, sourceAccountID)) {
                innerResult().code(PaymentV2ResultCode::REFERENCE_DUPLICATION);
                return false;
            }
            createReferenceEntry(mPaymentV2.reference, &delta, db);
        }

        int64 sourceSentUniversal;
        auto transferResult = accountManager.processTransfer(mSourceAccount, mSourceBalance,
                                                             mSourceSent, sourceSentUniversal);

        if (!processBalanceChange(app, transferResult))
            return false;

        if (!mDestBalance->addBalance(mDestReceived)) {
            innerResult().code(PaymentV2ResultCode::LINE_FULL);
            return false;
        }

        if (!processFees(app, delta, db)) {
            return false;
        }

        innerResult().paymentV2Response().destination = mDestBalance->getAccountID();
        innerResult().paymentV2Response().asset = mDestBalance->getAsset();
        EntryHelperProvider::storeChangeEntry(delta, db, mSourceBalance->mEntry);
        EntryHelperProvider::storeChangeEntry(delta, db, mDestBalance->mEntry);

        innerResult().paymentV2Response().paymentID = paymentID;

        return true;
    }

    bool PaymentOpV2Frame::doCheckValid(Application &app) {
        if (mPaymentV2.destination.type() != PaymentDestinationType::BALANCE) {
            return true;
        }

        if (mPaymentV2.sourceBalanceID == mPaymentV2.destination.balanceID()) {
            innerResult().code(PaymentV2ResultCode::MALFORMED);
            return false;
        }

        return true;
    }
}