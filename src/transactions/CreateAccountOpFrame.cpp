// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "transactions/CreateAccountOpFrame.h"
#include "ledger/LedgerDelta.h"
#include "database/Database.h"
#include "exsysidgen/ExternalSystemIDGenerators.h"
#include "main/Application.h"
#include "medida/meter.h"
#include "medida/metrics_registry.h"

namespace stellar
{
using namespace std;
using xdr::operator==;

CreateAccountOpFrame::CreateAccountOpFrame(Operation const& op,
                                           OperationResult& res,
                                           TransactionFrame& parentTx)
    : OperationFrame(op, res, parentTx)
    , mCreateAccount(mOperation.body.createAccountOp())
{
}

unordered_map<AccountID, CounterpartyDetails> CreateAccountOpFrame::
getCounterpartyDetails(Database& db, LedgerDelta* delta) const
{
    return {
        {
            mCreateAccount.destination,
            CounterpartyDetails({
                AccountType::NOT_VERIFIED, AccountType::GENERAL,
                AccountType::SYNDICATE
            }, true, false)
        }
    };
}

SourceDetails CreateAccountOpFrame::getSourceAccountDetails(
    std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails)
const
{
    const auto threshold = mSourceAccount->getMediumThreshold();
    uint32_t allowedSignerClass = 0;
    switch (mCreateAccount.accountType)
    {
    case AccountType::NOT_VERIFIED:
        allowedSignerClass = static_cast<int32_t>(SignerType::
            NOT_VERIFIED_ACC_MANAGER);
        break;
    case AccountType::GENERAL:
    case AccountType::SYNDICATE:
        if (mCreateAccount.policies != 0)
        {
            allowedSignerClass = static_cast<int32_t>(SignerType::
                GENERAL_ACC_MANAGER);
            break;
        }
        allowedSignerClass = static_cast<int32_t>(SignerType::
                                 GENERAL_ACC_MANAGER) |
                             static_cast<int32_t>(SignerType::
                                 NOT_VERIFIED_ACC_MANAGER);
        break;
    default:
        // it is not allowed to create or update any other account types
        allowedSignerClass = 0;
        break;
    }
    return SourceDetails({AccountType::MASTER}, threshold, allowedSignerClass);
}

void CreateAccountOpFrame::trySetReferrer(Application& app, Database& db,
                                          AccountFrame::pointer
                                          destAccountFrame) const
{
    destAccountFrame->setShareForReferrer(0);
    if (!mCreateAccount.referrer)
    {
        return;
    }

    const auto referrerAccountID = *mCreateAccount.referrer;
    if (referrerAccountID == app.getMasterID())
        return;

    const auto referrer = AccountFrame::loadAccount(referrerAccountID, db);
    if (!referrer)
        return;

    // amount is not applyable for referral fee
    auto referralFeeFrame = FeeFrame::loadForAccount(FeeType::REFERRAL_FEE,
                                                     app.getBaseAsset(),
                                                     FeeFrame::SUBTYPE_ANY,
                                                     referrer, 0, db);

    const auto referralFee = referralFeeFrame
                                 ? referralFeeFrame->getPercentFee()
                                 : 0;
    destAccountFrame->setShareForReferrer(referralFee);
    destAccountFrame->setReferrer(*mCreateAccount.referrer);
}

bool CreateAccountOpFrame::isAllowedToUpdateAccountType(
    const AccountFrame::pointer destAccount) const
{
    const auto accountType = destAccount->getAccountType();
    if (accountType == mCreateAccount.accountType)
        return true;
    if (accountType != AccountType::NOT_VERIFIED)
        return false;
    // it is only allowed to change account type from not verified to general or syndicate
    return mCreateAccount.accountType == AccountType::SYNDICATE ||
           mCreateAccount.accountType == AccountType::GENERAL;
}

void CreateAccountOpFrame::storeExternalSystemsIDs(Application& app,
    LedgerDelta& delta, Database& db, const AccountFrame::pointer account)
{
    auto generator = ExternalSystemIDGenerators(app, delta, db);
    auto newIDs = generator.generateNewIDs(account->getID());
    for (auto newID : newIDs)
    {
        newID->storeAdd(delta, db);
        innerResult().success().externalSystemIDs.push_back(newID->getExternalSystemAccountID());
    }
}


bool CreateAccountOpFrame::createAccount(Application& app, LedgerDelta& delta,
                                         LedgerManager& ledgerManager)
{
    auto& db = app.getDatabase();
    auto destAccountFrame = make_shared<AccountFrame
    >(mCreateAccount.destination);
    auto& destAccount = destAccountFrame->getAccount();

    destAccount.accountType = mCreateAccount.accountType;
    trySetReferrer(app, db, destAccountFrame);
    destAccount.policies = mCreateAccount.policies;

    destAccountFrame->storeAdd(delta, db);
    storeExternalSystemsIDs(app, delta, db, destAccountFrame);

    AccountManager accountManager(app, db, delta, ledgerManager);
    accountManager.createStats(destAccountFrame);
    // create balance for all availabe assets
    std::vector<AssetFrame::pointer> assets;
    AssetFrame::loadAssets(assets, db);
    for (auto baseAsset : assets)
    {
        BalanceID balanceID = mCreateAccount.destination;
        if (!(baseAsset->getCode() == app.getBaseAsset()))
            balanceID = BalanceKeyUtils::forAccount(mCreateAccount.destination,
                                                    delta.getHeaderFrame().
                                                          generateID(LedgerEntryType::BALANCE));
        auto balanceFrame = BalanceFrame::createNew(balanceID,
                                                    mCreateAccount.destination,
                                                    baseAsset->getCode(),
                                                    ledgerManager.
                                                    getCloseTime());
        balanceFrame->storeAdd(delta, db);
    }

    innerResult().success().referrerFee = destAccountFrame->
        getShareForReferrer();
    app.getMetrics().NewMeter({"op-create-account", "success", "apply"},
                              "operation").Mark();
    return true;
}

bool
CreateAccountOpFrame::doApply(Application& app,
                              LedgerDelta& delta, LedgerManager& ledgerManager)
{
    Database& db = ledgerManager.getDatabase();
    innerResult().code(CreateAccountResultCode::SUCCESS);

    auto destAccountFrame = AccountFrame::
        loadAccount(delta, mCreateAccount.destination, db);

    if (ledgerManager.getCurrentLedgerHeader().ledgerVersion < static_cast<
            int32_t>(mCreateAccount.ext.v()))
    {
        app.getMetrics().NewMeter({
            "op-create-account", "invalid", "invalid_account_version"
        }, "operation").Mark();
        innerResult().code(CreateAccountResultCode::INVALID_ACCOUNT_VERSION);
        return false;
    }

    if (!destAccountFrame)
    {
        return createAccount(app, delta, ledgerManager);
    }

    if (!isAllowedToUpdateAccountType(destAccountFrame))
    {
        app.getMetrics().NewMeter({
            "op-create-account", "invalid", "account-type-not-allowed"
        }, "operation").Mark();
        innerResult().code(CreateAccountResultCode::TYPE_NOT_ALLOWED);
        return false;
    }

    destAccountFrame->setAccountType(mCreateAccount.accountType);
    AccountEntry& accountEntry = destAccountFrame->getAccount();
    accountEntry.policies = mCreateAccount.policies;

    destAccountFrame->storeChange(delta, db);
    storeExternalSystemsIDs(app, delta, db, destAccountFrame);

    innerResult().success().referrerFee = destAccountFrame->
        getShareForReferrer();
    app.getMetrics().NewMeter({"op-create-account", "success", "apply"},
                              "operation").Mark();
    return true;
}

bool CreateAccountOpFrame::doCheckValid(Application& app)
{
    if (mCreateAccount.destination == getSourceID())
    {
        app.getMetrics().NewMeter({
                                      "op-create-account", "invalid",
                                      "malformed-destination-equals-source"
                                  },
                                  "operation").Mark();
        innerResult().code(CreateAccountResultCode::MALFORMED);
        return false;
    }

    if (mCreateAccount.accountType == AccountType::NOT_VERIFIED &&
        mCreateAccount.policies != 0)
    {
        app.getMetrics().NewMeter({
            "op-create-account", "invalid", "account-type-not-allowed"
        }, "operation").Mark();
        innerResult().code(CreateAccountResultCode::TYPE_NOT_ALLOWED);
        return false;
    }

    if (isSystemAccountType(mCreateAccount.accountType))
    {
        app.getMetrics().NewMeter({
            "op-create-account", "invalid", "account-type-not-allowed"
        }, "operation").Mark();
        innerResult().code(CreateAccountResultCode::TYPE_NOT_ALLOWED);
        return false;
    }

    return true;
}
}
