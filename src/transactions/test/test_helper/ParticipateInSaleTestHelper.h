#ifndef STELLAR_PARTICIPATE_IN_SALE_TEST_HELPER_H
#define STELLAR_PARTICIPATE_IN_SALE_TEST_HELPER_H

#include "ManageOfferTestHelper.h"

namespace stellar
{
namespace txtest
{

    class ParticipateInSaleTestHelper : public ManageOfferTestHelper
    {
    public:
        explicit ParticipateInSaleTestHelper(TestManager::pointer testManager);

    protected:
        void ensureDeleteSuccess(Account& source, ManageOfferOp op,
            ManageOfferSuccessResult success,
            LedgerDelta::KeyEntryMap& stateBeforeTx) override;
        void ensureCreateSuccess(Account& source, ManageOfferOp op,
            ManageOfferSuccessResult success,
            LedgerDelta::KeyEntryMap& stateBeforeTx) override;
    };
}

}


#endif //STELLAR_PARTICIPATE_IN_SALE_TEST_HELPER_H
