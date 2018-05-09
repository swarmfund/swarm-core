#pragma once

#include "TxHelper.h"

namespace stellar {
    namespace txtest {
        class SetFeesTestHelper : TxHelper {
        public:
            explicit SetFeesTestHelper(TestManager::pointer testManager);

            TransactionFramePtr createSetFeesTx(Account &source, FeeEntry *fee, bool isDelete);

            void applySetFeesTx(Account &source, FeeEntry *fee, bool isDelete,
                                SetFeesResultCode expectedResult = SetFeesResultCode::SUCCESS);

            FeeEntry createFeeEntry(FeeType type, AssetCode asset, int64_t fixed, int64_t percent,
                                    AccountID *accountID = nullptr, AccountType *accountType = nullptr,
                                    int64_t subtype = FeeFrame::SUBTYPE_ANY, int64_t lowerBound = 0,
                                    int64_t upperBound = INT64_MAX, AssetCode *feeAsset = nullptr);
        };
    }
}