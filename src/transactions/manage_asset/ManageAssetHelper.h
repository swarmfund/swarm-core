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
    static void createSystemBalances(AssetCode assetCode, Application &app, LedgerDelta &delta,
                                                                        uint64_t ledgerCloseTime);
};

}

#endif //STELLAR_MANAGEASSETHELPER_H
