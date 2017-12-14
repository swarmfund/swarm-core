// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "transactions/CreateAccountOpFrame.h"
#include "ledger/LedgerDelta.h"
#include "database/Database.h"

#include "ledger/AccountHelper.h"
#include "ledger/AssetHelper.h"

#include "exsysidgen/ExternalSystemIDGenerators.h"
#include "main/Application.h"
#include "medida/meter.h"
#include "medida/metrics_registry.h"

namespace stellar {
    using namespace std;
    using xdr::operator==;

    CreateAccountOpFrame::CreateAccountOpFrame(Operation const &op,
                                               OperationResult &res,
                                               TransactionFrame &parentTx)
            : OperationFrame(op, res, parentTx), mCreateAccount(mOperation.body.createAccountOp()) {

        detailsHelper[AccountType::NOT_VERIFIED] = [this]() {
            return static_cast<uint32_t>(SignerType::NOT_VERIFIED_ACC_MANAGER);
        };

        detailsHelper[AccountType::GENERAL] = [this]() {
            if (mCreateAccount.policies != 0) {
                return static_cast<uint32_t>(SignerType::GENERAL_ACC_MANAGER);
            }
            return static_cast<uint32_t>(SignerType::GENERAL_ACC_MANAGER)
                   | static_cast<uint32_t>(SignerType::NOT_VERIFIED_ACC_MANAGER);
        };

        detailsHelper[AccountType::SYNDICATE] = detailsHelper[AccountType::GENERAL];

    }

    unordered_map<AccountID, CounterpartyDetails> CreateAccountOpFrame::
    getCounterpartyDetails(Database &db, LedgerDelta *delta) const {
        return {
                {mCreateAccount.destination, CounterpartyDetails({AccountType::NOT_VERIFIED, AccountType::GENERAL,
                                                                  AccountType::SYNDICATE}, true, false)}
        };
    }

    SourceDetails CreateAccountOpFrame::getSourceAccountDetails(
            std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails)
    const {
        const auto threshold = mSourceAccount->getMediumThreshold();
        uint32_t allowedSignerClass = 0;
        auto allowedSignerClassHelper = detailsHelper.find(mCreateAccount.accountType);
        if (allowedSignerClassHelper != detailsHelper.end()) {
            allowedSignerClass = allowedSignerClassHelper->second();
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
        // it is only allowed to change account type from not verified to general or syndicate
        return mCreateAccount.accountType == AccountType::SYNDICATE ||
               mCreateAccount.accountType == AccountType::GENERAL;
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
        storeAccount<EntryHelperProvider::storeAddEntry>(app, delta, destAccountFrame);
        AccountManager accountManager(app, db, delta, ledgerManager);
        accountManager.createStats(destAccountFrame);
        // create balance for all available base assets
        createBalance(delta, db);
        return true;
    }

    void CreateAccountOpFrame::createBalance(LedgerDelta &delta, Database &db) {
        std::vector<AssetFrame::pointer> baseAssets;
        AssetHelper::Instance()->loadBaseAssets(baseAssets, db);
        for (const auto &baseAsset : baseAssets) {
            BalanceID balanceID = BalanceKeyUtils::forAccount(mCreateAccount.destination,
                                                              delta.getHeaderFrame().
                                                                      generateID(LedgerEntryType::BALANCE));
            auto balanceFrame = BalanceFrame::createNew(balanceID,
                                                        mCreateAccount.destination,
                                                        baseAsset->getCode());
            EntryHelperProvider::storeAddEntry(delta, db, balanceFrame->mEntry);
        }
    }

    bool
    CreateAccountOpFrame::doApply(Application &app,
                                  LedgerDelta &delta, LedgerManager &ledgerManager) {
        Database &db = ledgerManager.getDatabase();
        innerResult().code(CreateAccountResultCode::SUCCESS);
        app.getMetrics().NewMeter({"op-create-account", "success", "apply"},
                                  "operation").Mark();

        if (!ledgerManager.shouldUse(mCreateAccount.ext.v())) {
            app.getMetrics().NewMeter({
                                              "op-create-account", "invalid", "invalid_account_version"
                                      }, "operation").Mark();
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
            app.getMetrics().NewMeter({
                                              "op-create-account", "invalid", "account-type-not-allowed"
                                      }, "operation").Mark();
            innerResult().code(CreateAccountResultCode::TYPE_NOT_ALLOWED);
            return false;
        }

        storeAccount<EntryHelperProvider::storeChangeEntry>(app, delta, destAccountFrame);
        return true;
    }

    bool CreateAccountOpFrame::doCheckValid(Application &app) {
        if (mCreateAccount.destination == getSourceID()) {
            app.getMetrics().NewMeter({
                                              "op-create-account", "invalid",
                                              "malformed-destination-equals-source"
                                      },
                                      "operation").Mark();
            innerResult().code(CreateAccountResultCode::MALFORMED);
            return false;
        }

        if (mCreateAccount.accountType == AccountType::NOT_VERIFIED &&
            mCreateAccount.policies != 0) {
            app.getMetrics().NewMeter({
                                              "op-create-account", "invalid", "account-type-not-allowed"
                                      }, "operation").Mark();
            innerResult().code(CreateAccountResultCode::TYPE_NOT_ALLOWED);
            return false;
        }

        if (isSystemAccountType(mCreateAccount.accountType)) {
            app.getMetrics().NewMeter({
                                              "op-create-account", "invalid", "account-type-not-allowed"
                                      }, "operation").Mark();
            innerResult().code(CreateAccountResultCode::TYPE_NOT_ALLOWED);
            return false;
        }

        return true;
    }

    template<CreateAccountOpFrame::Store storeAccount>
    void
    CreateAccountOpFrame::storeAccount(Application &app, LedgerDelta &delta, AccountFrame::pointer destAccountFrame) {
        auto &db = app.getDatabase();
        auto &destAccount = destAccountFrame->getAccount();
        destAccount.accountType = mCreateAccount.accountType;
        trySetReferrer(app, db, destAccountFrame);
        destAccount.policies = mCreateAccount.policies;
        storeAccount(delta, db, destAccountFrame->mEntry);
        storeExternalSystemsIDs(app, delta, db, destAccountFrame);
    }


}
