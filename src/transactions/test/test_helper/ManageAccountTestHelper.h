//
// Created by volodymyr on 26.01.18.
//

#ifndef STELLAR_MANAGEACCOUNTTESTHELPER_H
#define STELLAR_MANAGEACCOUNTTESTHELPER_H

#include "TxHelper.h"

namespace stellar {

namespace txtest {

class ManageAccountTestHelper : public TxHelper {
private:
    typedef std::vector<BlockReasons> Reasons;
    uint32_t sumBlockReasons(const Reasons& reasons);
public:
    explicit ManageAccountTestHelper(TestManager::pointer testManager);

    TransactionFramePtr createManageAccountTx(Account root, AccountID destination, AccountType accountType,
                                              Reasons toAdd, Reasons toRemove);

    ManageAccountResult applyManageAccount(Account &root, AccountID destination, AccountType accountType,
                                           Reasons toAdd, Reasons toRemove,
                                           ManageAccountResultCode expectedResult = ManageAccountResultCode::SUCCESS);
};


}
}


#endif //STELLAR_MANAGEACCOUNTTESTHELPER_H
