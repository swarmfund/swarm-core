#pragma once

#include "TxHelper.h"

namespace stellar {
    namespace txtest {
        class ManageSaleTestHelper : TxHelper {
        public:
            explicit ManageSaleTestHelper(TestManager::pointer testManager);

            ManageSaleOp::_data_t createDataForUpdateSaleDetails(uint64_t requestID, std::string newDetails);

            TransactionFramePtr createManageSaleTx(Account &source, uint64_t saleID, ManageSaleOp::_data_t data);

            ManageSaleResult applyManageSaleTx(Account &source, uint64_t saleID, ManageSaleOp::_data_t data,
                                               ManageSaleResultCode expectedResultCode = ManageSaleResultCode::SUCCESS);
        };
    }
}