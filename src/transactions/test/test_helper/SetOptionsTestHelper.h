#pragma once

// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "overlay/StellarXDR.h"
#include "util/optional.h"
#include "TxHelper.h"

namespace stellar
{
namespace txtest
{
    struct ThresholdSetter
    {
        optional<uint8_t> masterWeight;
        optional<uint8_t> lowThreshold;
        optional<uint8_t> medThreshold;
        optional<uint8_t> highThreshold;
    };

    class SetOptionsTestHelper : TxHelper
    {
    public:
        explicit SetOptionsTestHelper(TestManager::pointer testManager);

        TransactionFramePtr createSetOptionsTx(Account &source, ThresholdSetter *thresholdSetter,
                                               Signer *signer, TrustData *trustData = nullptr,
                                               LimitsUpdateRequestData *limitsUpdateRequestData = nullptr);

        void applySetOptionsTx(Account &source, ThresholdSetter *thresholdSetter,
                               Signer *signer, TrustData *trustData = nullptr,
                               LimitsUpdateRequestData *limitsUpdateRequestData = nullptr,
                               SetOptionsResultCode expectedResult = SetOptionsResultCode::SUCCESS,
                               SecretKey *txSigner = nullptr);
    };
}
}