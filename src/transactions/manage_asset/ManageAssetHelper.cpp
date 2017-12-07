#include "ManageAssetHelper.h"

namespace stellar
{

void ManageAssetHelper::createSystemBalances(AssetCode assetCode, Application &app,
                                            LedgerDelta &delta, uint64_t ledgerCloseTime)
{
    auto systemAccounts = app.getSystemAccounts();

    for (auto& systemAccount : systemAccounts)
    {
        auto balanceFrame = BalanceFrame::loadBalance(systemAccount, assetCode, app.getDatabase(), &delta);
        if (!balanceFrame) {
            BalanceID balanceID = BalanceKeyUtils::forAccount(systemAccount,
                                                              delta.getHeaderFrame().generateID(LedgerEntryType::BALANCE));
            balanceFrame = BalanceFrame::createNew(balanceID, systemAccount, assetCode, ledgerCloseTime);

            balanceFrame->storeAdd(delta, app.getDatabase());
        }
    }
}

}


