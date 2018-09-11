// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "transactions/ManageBalanceOpFrame.h"
#include "ledger/LedgerDelta.h"
#include "ledger/LedgerHeaderFrame.h"
#include "ledger/AccountHelper.h"
#include "ledger/AssetHelper.h"
#include "ledger/BalanceHelper.h"
#include "database/Database.h"
#include "main/Application.h"
#include "medida/meter.h"
#include "medida/metrics_registry.h"

namespace stellar
{
using namespace std;
using xdr::operator==;

std::unordered_map<AccountID, CounterpartyDetails> ManageBalanceOpFrame::
getCounterpartyDetails(Database& db, LedgerDelta* delta) const
{
    const std::vector<AccountType> allowedCounterparties = {
        AccountType::GENERAL, AccountType::NOT_VERIFIED, AccountType::SYNDICATE, AccountType::VERIFIED,
        AccountType::EXCHANGE, AccountType::ACCREDITED_INVESTOR, AccountType::INSTITUTIONAL_INVESTOR
    };

    return {
        {
            mManageBalance.destination,
            CounterpartyDetails(allowedCounterparties, true, true)
        }
    };
}

SourceDetails ManageBalanceOpFrame::getSourceAccountDetails(
    std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
    int32_t ledgerVersion) const
{
    std::vector<AccountType> allowedSourceAccounts;
    if (getSourceID() == mManageBalance.destination)
        allowedSourceAccounts = {
            AccountType::GENERAL, AccountType::NOT_VERIFIED,
            AccountType::SYNDICATE, AccountType::EXCHANGE, AccountType::VERIFIED,
            AccountType::ACCREDITED_INVESTOR, AccountType::INSTITUTIONAL_INVESTOR
        };
    else
        allowedSourceAccounts = {AccountType::MASTER};
    return SourceDetails(allowedSourceAccounts,
                         mSourceAccount->getLowThreshold(),
                         static_cast<int32_t>(SignerType::BALANCE_MANAGER),
                         static_cast<int32_t>(BlockReasons::TOO_MANY_KYC_UPDATE_REQUESTS) |
                         static_cast<uint32_t>(BlockReasons::WITHDRAWAL));
}

ManageBalanceOpFrame::ManageBalanceOpFrame(Operation const& op,
                                           OperationResult& res,
                                           TransactionFrame& parentTx)
    : OperationFrame(op, res, parentTx)
    , mManageBalance(mOperation.body.manageBalanceOp())
{
}

bool
ManageBalanceOpFrame::doApply(Application& app,
                              LedgerDelta& delta, LedgerManager& ledgerManager)
{
    Database& db = ledgerManager.getDatabase();
    if (mManageBalance.action == ManageBalanceAction::CREATE_UNIQUE)
    {
        if (!ledgerManager.shouldUse(LedgerVersion::UNIQUE_BALANCE_CREATION))
        {
            innerResult().code(ManageBalanceResultCode::VERSION_IS_NOT_SUPPORTED_YET);
            return false;
        }

        const auto balance = BalanceHelper::Instance()->loadBalance(mManageBalance.destination, mManageBalance.asset, db, nullptr);
        if (!!balance)
        {
            innerResult().code(ManageBalanceResultCode::BALANCE_ALREADY_EXISTS);
            return false;
        }
    }

    auto accountHelper = AccountHelper::Instance();
    const AccountFrame::pointer destAccountFrame = accountHelper->
        loadAccount(delta, mManageBalance.destination, db);
    if (!destAccountFrame)
    {
        innerResult().code(ManageBalanceResultCode::DESTINATION_NOT_FOUND);
        return false;
    }

    auto balanceHelper = BalanceHelper::Instance();
    auto balanceFrame = balanceHelper->loadBalance(mManageBalance.destination,
                                                   mManageBalance.asset, db,
                                                   &delta);

    auto assetHelper = AssetHelper::Instance();
    const auto assetFrame = assetHelper->loadAsset(mManageBalance.asset, db);
    if (!assetFrame)
    {
        innerResult().code(ManageBalanceResultCode::ASSET_NOT_FOUND);
        return false;
    }

    const BalanceID newBalanceID = BalanceKeyUtils::forAccount(mManageBalance.
                                                               destination,
                                                               delta.
                                                               getHeaderFrame().
                                                               generateID(LedgerEntryType
                                                                          ::
                                                                          BALANCE));
    balanceFrame = BalanceFrame::createNew(newBalanceID,
                                           mManageBalance.destination,
                                           mManageBalance.asset);
    EntryHelperProvider::storeAddEntry(delta, db, balanceFrame->mEntry);
    innerResult().success().balanceID = newBalanceID;
    innerResult().code(ManageBalanceResultCode::SUCCESS);
    return true;
}

bool ManageBalanceOpFrame::doCheckValid(Application& app)
{
    if (mManageBalance.action == ManageBalanceAction::DELETE_BALANCE)
    {
        innerResult().code(ManageBalanceResultCode::MALFORMED);
        return false;
    }

    if (!AssetFrame::isAssetCodeValid(mManageBalance.asset))
    {
        innerResult().code(ManageBalanceResultCode::INVALID_ASSET);
        return false;
    }

    return true;
}
}
