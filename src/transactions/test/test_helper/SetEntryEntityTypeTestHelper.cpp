#include <transactions/SetEntityTypeOpFrame.h>
#include <lib/catch.hpp>
#include <ledger/EntityTypeHelper.h>
#include "SetEntryEntityTypeTestHelper.h"
#include "TxHelper.h"


namespace stellar {
    namespace txtest {
        SetEntryEntityTypeTestHelper::SetEntryEntityTypeTestHelper(txtest::TestManager::pointer testManager)
                : TxHelper(testManager) {}

        TransactionFramePtr SetEntryEntityTypeTestHelper::createSetEntityTypeTx(txtest::Account &source,
                                                                                EntityTypeEntry entityType,
                                                                                bool isDelete)
        {
            Operation op;
            op.body.type(OperationType::SET_ENTITY_TYPE);

            SetEntityTypeOp &setEntityTypeOp = op.body.setEntityTypeOp();

            setEntityTypeOp.entityType = entityType;

            setEntityTypeOp.isDelete = isDelete;

            return TxHelper::txFromOperation(source, op, nullptr);
        }

        SetEntityTypeResult SetEntryEntityTypeTestHelper::applySetEntityTypeTx(txtest::Account &source,
                                                                               EntityTypeEntry entityType, bool isDelete,
                                                                               SetEntityTypeResultCode expectedResult)
        {
            TransactionFramePtr txFrame;
            txFrame = createSetEntityTypeTx(source, entityType, isDelete);
            mTestManager->applyCheck(txFrame);

            auto actualResult = SetEntityTypeOpFrame::getInnerCode(txFrame->getResult().result.results()[0]);

            REQUIRE(actualResult == expectedResult);

            if (actualResult != SetEntityTypeResultCode::SUCCESS) {
                return SetEntityTypeResult{};
            }

            auto entityTypeHelper = EntityTypeHelper::Instance();

            auto db = mTestManager->getDB();

            if (isDelete){
                REQUIRE(!entityTypeHelper->exists(db, entityType.id, entityType.type));
                return
            }

            auto entityTypeFrame = entityTypeHelper->loadEntityType(entityType.id, entityType.type, db);

            REQUIRE(!!entityTypeFrame);
            REQUIRE(entityTypeFrame->getEntityTypeID() == entityType.id);
            REQUIRE(entityTypeFrame->getEntityTypeValue() == entityType.type);
            return
        }

        EntityTypeEntry
        SetEntryEntityTypeTestHelper::createEntityTypeEntry(int32_t type, uint64_t id, std::string name)
        {
            EntityTypeEntry entityTypeEntry;
            entityTypeEntry.type = static_cast<EntityType>(type);
            entityTypeEntry.id = id;
            entityTypeEntry.name = name;

            return entityTypeEntry;
        }
    }
}