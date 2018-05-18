
#ifndef STELLAR_SETENTRYENTITYTYPETESTHELPER_H
#define STELLAR_SETENTRYENTITYTYPETESTHELPER_H


#include <xdr/Stellar-operation-set-entity-type.h>
#include "TxHelper.h"

namespace stellar
{

namespace txtest
{

class SetEntryEntityTypeTestHelper : public TxHelper
{
    public:
        explicit SetEntryEntityTypeTestHelper(TestManager::pointer testManager);

        SetEntityTypeResult
        applySetEntityTypeTx(Account &source, EntityTypeEntry entityType, bool isDelete,
                             SetEntityTypeResultCode  expectedResult = SetEntityTypeResultCode::SUCCESS);

        EntityTypeEntry
        SetEntryEntityTypeTestHelper::createEntityTypeEntry(int32_t type, uint64_t id, std::string name);


        TransactionFramePtr createSetEntityTypeTx(Account& source,EntityTypeEntry entityType, bool isDelete);
};
}

}

#endif //STELLAR_SETENTRYENTITYTYPETESTHELPER_H