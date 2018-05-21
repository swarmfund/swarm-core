#pragma once

#include "TxHelper.h"

namespace stellar
{

namespace txtest
{

class SetEntryEntityTypeTestHelper : public TxHelper
{
    public:
        explicit SetEntryEntityTypeTestHelper(TestManager::pointer testManager);

        void applySetEntityTypeTx(Account &source, EntityTypeEntry entityType, bool isDelete,
                             SetEntityTypeResultCode expectedResult = SetEntityTypeResultCode::SUCCESS);

        EntityTypeEntry createEntityTypeEntry(EntityType type, uint64_t id, std::string name);


        TransactionFramePtr createSetEntityTypeTx(Account& source,EntityTypeEntry entityType, bool isDelete);
};
}

}