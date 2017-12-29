#include <ledger/BalanceHelper.h>
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

}


