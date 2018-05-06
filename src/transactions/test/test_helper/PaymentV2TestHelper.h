#pragma once

#include "TxHelper.h"

namespace stellar {
    namespace txtest {
        struct BalanceDelta {
            AssetCode asset;
            int64_t amountDelta;
        };

        struct PaymentV2Delta {
            std::vector<BalanceDelta> source;
            std::vector<BalanceDelta> destination;
            std::vector<BalanceDelta> commission;
        };

        class PaymentV2TestHelper : TxHelper {
        public:
            explicit PaymentV2TestHelper(TestManager::pointer testManager);

            TransactionFramePtr createPaymentV2Tx(Account &source, BalanceID sourceBalanceID,
                                                  PaymentOpV2::_destination_t destination, uint64_t amount,
                                                  PaymentFeeDataV2 feeData, std::string subject, std::string reference);

            PaymentV2Result applyPaymentV2Tx(Account &source, BalanceID sourceBalanceID,
                                             PaymentOpV2::_destination_t destination, uint64_t amount,
                                             PaymentFeeDataV2 feeData, std::string subject, std::string reference,
                                             PaymentV2Delta *paymentDelta = nullptr,
                                             PaymentV2ResultCode expectedResultCode = PaymentV2ResultCode::SUCCESS);

            PaymentOpV2::_destination_t createDestinationForAccount(AccountID destAccountID);

            PaymentOpV2::_destination_t createDestinationForBalance(BalanceID destBalanceID);

            FeeDataV2 createFeeData(uint64 fixedFee, uint64 paymentFee, AssetCode feeAsset);

            PaymentFeeDataV2 createPaymentFeeData(FeeDataV2 sourceFeeData, FeeDataV2 destFeeData,
                                                  bool sourcePaysForDest);
        };
    }
}