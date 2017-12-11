#ifndef STELLAR_MANAGEASSETHELPER_H
#define STELLAR_MANAGEASSETHELPER_H

#include <xdr/Stellar-types.h>
#include <main/Application.h>
#include <ledger/LedgerDelta.h>
#include <ledger/BalanceFrame.h>

namespace stellar
{

class ManageAssetHelper
{
public:
    static void createSystemBalances(AssetCode assetCode, Application &app, LedgerDelta &delta);

    static void createBalanceForAccount(AccountID account, AssetCode assetCode, Application &app, LedgerDelta &delta);
};

}

#endif //STELLAR_MANAGEASSETHELPER_H
