#pragma once

#include "TxHelper.h"

namespace stellar {
    namespace txtest {
        class ManageSaleTestHelper : TxHelper {
        public:
            explicit ManageSaleTestHelper(TestManager::pointer testManager);

            ManageSaleOp::_data_t createDataForAction(ManageSaleAction action, uint64_t *requestID = nullptr,
                                                      std::string *newDetails = nullptr);

            ManageSaleOp::_data_t setSaleState(SaleState saleState);

            ManageSaleOp::_data_t createUpdateSaleEndTimeRequest(uint64_t requestID, uint64_t newEndTime);

            ManageSaleOp::_data_t
            createPromotionUpdateRequest(uint64_t requestID, SaleCreationRequest newPromotionData);

            TransactionFramePtr createManageSaleTx(Account &source, uint64_t saleID, ManageSaleOp::_data_t data);

            ManageSaleResult applyManageSaleTx(Account &source, uint64_t saleID, ManageSaleOp::_data_t data,
                                               ManageSaleResultCode expectedResultCode = ManageSaleResultCode::SUCCESS);
        };
    }
}