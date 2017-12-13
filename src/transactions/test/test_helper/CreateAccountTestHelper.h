#ifndef STELLAR_CREATEACCOUNTTESTHELPER_H
#define STELLAR_CREATEACCOUNTTESTHELPER_H

#include "TxHelper.h"

namespace stellar
{
namespace txtest
{
    class CreateAccountTestHelper : TxHelper
    {
    public:
        explicit CreateAccountTestHelper(TestManager::pointer testManager);

        CreateAccountResultCode applyCreateAccountTx(Account &from, PublicKey to, AccountType accountType,
                                                     Account* signer = nullptr, AccountID *referrer = nullptr,
                                                     int32 policies = -1,
                                                     CreateAccountResultCode expectedResult = CreateAccountResultCode::SUCCESS);

        TransactionFramePtr createCreateAccountTx(Account &from, PublicKey &to, AccountType accountType,
                                                  Account* signer = nullptr, AccountID* referrer = nullptr,
                                                  int32 policies = -1);

    };
}

}


#endif //STELLAR_CREATEACCOUNTTESTHELPER_H
