#include <ledger/BalanceHelperLegacy.h>
#include <ledger/LedgerHeaderFrame.h>
#include "ManageAssetHelper.h"
#include "transactions/AccountManager.h"

namespace stellar
{

void ManageAssetHelper::createSystemBalances(AssetCode assetCode, Application &app,
                                            LedgerDelta &delta)
{
    auto systemAccounts = app.getSystemAccounts();

    auto& db = app.getDatabase();
    for (auto& systemAccount : systemAccounts)
    {

        AccountManager::loadOrCreateBalanceForAsset(systemAccount, assetCode, db, delta);
    }
}

void ManageAssetHelper::createBalanceForAccount(AccountID account,
    AssetCode assetCode, Application& app, LedgerDelta& delta)
{
    createBalanceForAccount(
            account,
            assetCode,
            app.getDatabase(),
            delta
    );
}

    void ManageAssetHelper::createBalanceForAccount(AccountID account, AssetCode assetCode, Database &db,
                                                    LedgerDelta &delta) {
        auto balanceFrame = BalanceHelperLegacy::Instance()->loadBalance(account, assetCode, db, &delta);
        if (!!balanceFrame)
            return;
        const BalanceID balanceID = BalanceKeyUtils::forAccount(account, delta.getHeaderFrame().generateID(LedgerEntryType::BALANCE));
        balanceFrame = BalanceFrame::createNew(balanceID, account, assetCode);

        EntryHelperProvider::storeAddEntry(delta, db, balanceFrame->mEntry);
    }
}


