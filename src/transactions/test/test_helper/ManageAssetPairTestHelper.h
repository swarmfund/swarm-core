#ifndef STELLAR_MANAGEASSETPAIRTESTHELPER_H
#define STELLAR_MANAGEASSETPAIRTESTHELPER_H

#include "TxHelper.h"
#include <ledger/AssetPairFrame.h>
#include <ledger/AssetPairHelper.h>
#include <transactions/ManageAssetPairOpFrame.h>

namespace stellar
{

namespace txtest
{
    class ManageAssetPairTestHelper:TxHelper
    {
    private:
        AssetPairEntry calculateExpectedAssetPair(const ManageAssetPairOp& op, AssetPairFrame::pointer assetPairFrameBefore,
                                                  ManageAssetPairAction action);
    public:
        explicit ManageAssetPairTestHelper(TestManager::pointer testManager);

        TransactionFramePtr createManageAssetPairTx(Account &source, AssetCode base, AssetCode quote,
                                                    int64_t physicalPrice,
                                                    int64_t physicalPriceCorrection, int64_t maxPriceStep,
                                                    int32_t policies = 0,
                                                    ManageAssetPairAction action = ManageAssetPairAction::CREATE,
                                                    Account *signer = nullptr);

        ManageAssetPairResultCode applyManageAssetPairTx(Account &source, AssetCode base, AssetCode quote,
                                                         int64_t physicalPrice, int64_t physicalPriceCorrection,
                                                         int64_t maxPriceStep, int32_t policies = 0,
                                                         ManageAssetPairAction action = ManageAssetPairAction::CREATE,
                                                         Account* signer = nullptr,
                                                         ManageAssetPairResultCode expectedResult = ManageAssetPairResultCode::SUCCESS);

        void createAssetPair(Account &source, AssetCode base, AssetCode quote, int64_t physicalPrice,
                             int64_t physicalPriceCorrection = 0, int64_t maxPriceStep = 0, int32_t policies = 0);

    };
}

}




#endif //STELLAR_MANAGEASSETPAIRTESTHELPER_H
