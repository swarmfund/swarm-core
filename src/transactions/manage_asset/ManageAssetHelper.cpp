#include <ledger/BalanceHelper.h>
#include "ManageAssetHelper.h"

namespace stellar
{

void ManageAssetHelper::createSystemBalances(AssetCode assetCode, Application &app,
                                            LedgerDelta &delta)
{
    auto systemAccounts = app.getSystemAccounts();

    auto balanceHelper = BalanceHelper::Instance();
    for (auto& systemAccount : systemAccounts)
    {
        createBalanceForAccount(systemAccount, assetCode, app, delta);
    }
}

void ManageAssetHelper::createBalanceForAccount(AccountID account,
    AssetCode assetCode, Application& app, LedgerDelta& delta)
{
    auto balanceFrame = BalanceHelper::Instance()->loadBalance(account, assetCode, app.getDatabase(), &delta);
    if (!!balanceFrame)
        return;
    const BalanceID balanceID = BalanceKeyUtils::forAccount(account, delta.getHeaderFrame().generateID(LedgerEntryType::BALANCE));
    balanceFrame = BalanceFrame::createNew(balanceID, account, assetCode);

    EntryHelperProvider::storeAddEntry(delta, app.getDatabase(), balanceFrame->mEntry);
}
}


