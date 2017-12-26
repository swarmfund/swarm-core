#ifndef STELLAR_MANAGE_OFFER_TEST_HELPER_H
#define STELLAR_MANAGE_OFFER_TEST_HELPER_H

#include "TxHelper.h"

namespace stellar
{
namespace txtest
{


    class ManageOfferTestHelper : TxHelper
    {
    public:
        explicit ManageOfferTestHelper(TestManager::pointer testManager);

        ManageOfferResult applyManageOffer(Account& source, uint64_t offerID, BalanceID const& baseBalance,
            BalanceID const& quoteBalance, int64_t amount, int64_t price, bool isBuy, int64_t fee,
            ManageOfferResultCode expectedResult = ManageOfferResultCode::SUCCESS);

        TransactionFramePtr creatManageOfferTx(Account& source, uint64_t offerID, BalanceID const& baseBalance,
            BalanceID const& quoteBalance, int64_t amount, int64_t price, bool isBuy, int64_t fee);

    };
}

}


#endif //STELLAR_MANAGE_OFFER_TEST_HELPER_H
