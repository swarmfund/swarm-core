// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include <transactions/manage_asset/ManageAssetHelper.h>
#include "transactions/CreateAccountOpFrame.h"
#include "ledger/LedgerDelta.h"
#include "database/Database.h"

#include "ledger/AccountHelper.h"
#include "ledger/AssetHelperLegacy.h"

#include "exsysidgen/ExternalSystemIDGenerators.h"
#include "main/Application.h"

namespace stellar {
    using namespace std;
    using xdr::operator==;

    CreateAccountOpFrame::CreateAccountOpFrame(Operation const &op,
                                               OperationResult &res,
                                               TransactionFrame &parentTx)
            : OperationFrame(op, res, parentTx), mCreateAccount(mOperation.body.createAccountOp()) {
    }

    unordered_map<AccountID, CounterpartyDetails> CreateAccountOpFrame::
    getCounterpartyDetails(Database &db, LedgerDelta *delta) const {
        return {
                {mCreateAccount.destination, CounterpartyDetails({AccountType::NOT_VERIFIED, AccountType::GENERAL,
                                                                  AccountType::SYNDICATE, AccountType::EXCHANGE,
                                                                  AccountType::VERIFIED}, true, false)}
        };
    }

    SourceDetails CreateAccountOpFrame::getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
                                                                    int32_t ledgerVersion)
    const {
        const auto threshold = mSourceAccount->getMediumThreshold();
        uint32_t allowedSignerClass = 0;
        switch (mCreateAccount.accountType) {
            case AccountType::NOT_VERIFIED:
                allowedSignerClass = static_cast<int32_t>(SignerType::
                NOT_VERIFIED_ACC_MANAGER);
                break;
            case AccountType::VERIFIED:
            case AccountType::GENERAL:
                if (mCreateAccount.policies != 0) {
                    allowedSignerClass = static_cast<int32_t>(SignerType::
                    GENERAL_ACC_MANAGER);
                    break;
                }
                allowedSignerClass = static_cast<int32_t>(SignerType::
                GENERAL_ACC_MANAGER) |
                                     static_cast<int32_t>(SignerType::
                                     NOT_VERIFIED_ACC_MANAGER);
                break;
            case AccountType::SYNDICATE:
                if (mCreateAccount.policies != 0) {
                    allowedSignerClass = static_cast<int32_t>(SignerType::SYNDICATE_ACC_MANAGER);
                    break;
                }
                allowedSignerClass = static_cast<int32_t>(SignerType::SYNDICATE_ACC_MANAGER) |
                                     static_cast<int32_t>(SignerType::NOT_VERIFIED_ACC_MANAGER);
                break;
            case AccountType::EXCHANGE:
                allowedSignerClass = static_cast<int32_t>(SignerType::EXCHANGE_ACC_MANAGER);
                break;

            default:
                // it is not allowed to create or update any other account types
                allowedSignerClass = 0;
                break;
        }
        return SourceDetails({AccountType::MASTER}, threshold, allowedSignerClass);
    }

    void CreateAccountOpFrame::trySetReferrer(Application &app, Database &db,
                                              AccountFrame::pointer
                                              destAccountFrame) const {
        if (!mCreateAccount.referrer) {
            return;
        }

        const auto referrerAccountID = *mCreateAccount.referrer;
        if (referrerAccountID == app.getMasterID())
            return;

        if (!AccountHelper::Instance()->exists(referrerAccountID, db))
            return;

        destAccountFrame->setReferrer(referrerAccountID);
    }

    bool CreateAccountOpFrame::isAllowedToUpdateAccountType(
            const AccountFrame::pointer destAccount) const {
        const auto accountType = destAccount->getAccountType();
        if (accountType == mCreateAccount.accountType)
            return true;
        if (accountType != AccountType::NOT_VERIFIED)
            return false;
        // it is only allowed to change account type from not verified to general or syndicate or verified
        return mCreateAccount.accountType == AccountType::SYNDICATE ||
               mCreateAccount.accountType == AccountType::GENERAL ||
               mCreateAccount.accountType == AccountType::EXCHANGE ||
               mCreateAccount.accountType == AccountType::VERIFIED;
    }

    void CreateAccountOpFrame::storeExternalSystemsIDs(Application &app,
                                                       LedgerDelta &delta, Database &db,
                                                       const AccountFrame::pointer account) {
        auto generator = ExternalSystemIDGenerators(app, delta, db);
        auto newIDs = generator.generateNewIDs(account->getID());
        for (const auto &newID : newIDs) {
            EntryHelperProvider::storeAddEntry(delta, db, newID->mEntry);
            innerResult().success().externalSystemIDs.push_back(newID->getExternalSystemAccountID());
        }
    }

    bool CreateAccountOpFrame::createAccount(Application &app, LedgerDelta &delta,
                                             LedgerManager &ledgerManager) {
        auto &db = app.getDatabase();
        auto destAccountFrame = make_shared<AccountFrame>(mCreateAccount.destination);
        buildAccount(app, delta, destAccountFrame);

        //save recovery accountID
        destAccountFrame->setRecoveryID(mCreateAccount.recoveryKey);

        EntryHelperProvider::storeAddEntry(delta, db, destAccountFrame->mEntry);
        AccountManager accountManager(app, db, delta, ledgerManager);
        accountManager.createStats(destAccountFrame);
        // create balance for all available base assets
        createBalance(delta, db);
        return true;
    }

    void CreateAccountOpFrame::createBalance(LedgerDelta &delta, Database &db) {
        std::vector<AssetFrame::pointer> baseAssets;
        AssetHelperLegacy::Instance()->loadBaseAssets(baseAssets, db);
        for (const auto &baseAsset : baseAssets) {
            ManageAssetHelper::createBalanceForAccount(mCreateAccount.destination, baseAsset->getCode(), db, delta);
        }
    }

    bool
    CreateAccountOpFrame::doApply(Application &app,
                                  LedgerDelta &delta, LedgerManager &ledgerManager) {
        Database &db = ledgerManager.getDatabase();
        innerResult().code(CreateAccountResultCode::SUCCESS);

        if (!ledgerManager.shouldUse(mCreateAccount.ext.v())) {
            innerResult().code(CreateAccountResultCode::INVALID_ACCOUNT_VERSION);
            return false;
        }

        auto destAccountFrame = AccountHelper::Instance()->loadAccount(delta, mCreateAccount.destination, db);
        if (!destAccountFrame) {
            return createAccount(app, delta, ledgerManager);
        }

        return tryUpdateAccountType(app, delta, db, destAccountFrame);
    }

    bool CreateAccountOpFrame::tryUpdateAccountType(Application &app, LedgerDelta &delta, Database &db,
                                                    AccountFrame::pointer &destAccountFrame) {
        if (!isAllowedToUpdateAccountType(destAccountFrame)) {
            innerResult().code(CreateAccountResultCode::TYPE_NOT_ALLOWED);
            return false;
        }

        buildAccount(app, delta, destAccountFrame);
        EntryHelperProvider::storeChangeEntry(delta, db, destAccountFrame->mEntry);
        return true;
    }

    bool CreateAccountOpFrame::doCheckValid(Application &app) {
        if (mCreateAccount.destination == getSourceID()) {
            innerResult().code(CreateAccountResultCode::MALFORMED);
            return false;
        }

        if (mCreateAccount.recoveryKey == mCreateAccount.destination) {
            innerResult().code(CreateAccountResultCode::MALFORMED);
            return false;
        }

        if (mCreateAccount.accountType == AccountType::NOT_VERIFIED &&
            mCreateAccount.policies != 0) {
            innerResult().code(CreateAccountResultCode::NOT_VERIFIED_CANNOT_HAVE_POLICIES);
            return false;
        }

        if (isSystemAccountType(mCreateAccount.accountType)) {
            innerResult().code(CreateAccountResultCode::TYPE_NOT_ALLOWED);
            return false;
        }

        return true;
    }

    void
    CreateAccountOpFrame::buildAccount(Application &app, LedgerDelta &delta, AccountFrame::pointer destAccountFrame) {
        auto &db = app.getDatabase();
        auto &destAccount = destAccountFrame->getAccount();
        destAccount.accountType = mCreateAccount.accountType;
        trySetReferrer(app, db, destAccountFrame);
        destAccount.policies = mCreateAccount.policies;
        storeExternalSystemsIDs(app, delta, db, destAccountFrame);
        if (mCreateAccount.ext.v() == LedgerVersion::REPLACE_ACCOUNT_TYPES_WITH_POLICIES &&
            mCreateAccount.ext.opExt().roleID)
        {
            destAccount.ext.v(LedgerVersion::REPLACE_ACCOUNT_TYPES_WITH_POLICIES);
            destAccount.ext.accountEntryExt().accountRole.activate() = *mCreateAccount.ext.opExt().roleID;
        }
    }
}
