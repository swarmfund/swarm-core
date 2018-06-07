#pragma once

#include "TxHelper.h"

namespace stellar {
    namespace txtest {
        class ManageSaleTestHelper : TxHelper {
        public:
            explicit ManageSaleTestHelper(TestManager::pointer testManager);

            ManageSaleOp::_data_t createDataForAction(ManageSaleAction action, uint64_t *requestID = nullptr,
                                                      std::string *newDetails = nullptr);

            TransactionFramePtr createManageSaleTx(Account &source, uint64_t saleID, ManageSaleOp::_data_t data);

            ManageSaleResult applyManageSaleTx(Account &source, uint64_t saleID, ManageSaleOp::_data_t data,
                                               ManageSaleResultCode expectedResultCode = ManageSaleResultCode::SUCCESS);
        };
    }
}