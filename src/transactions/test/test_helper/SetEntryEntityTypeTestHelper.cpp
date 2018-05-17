#include <transactions/SetEntityTypeOpFrame.h>
#include <lib/catch.hpp>
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

            if (EntityType::) {
               // auto storedFee = FeeHelper::Instance()->loadFee(fee->feeType, fee->asset,
                                                                fee->accountID.get(), fee->accountType.get(),
                                                                fee->subtype, fee->lowerBound, fee->upperBound,
                                                                mTestManager->getDB());
                if (isDelete)
                    REQUIRE(!storedFee);
                else {
                    REQUIRE(storedFee);
                    REQUIRE(storedFee->getFee() == *fee);
                }
            }
        }

        FeeEntry
        SetEntryEntityTypeTestHelper::createFeeEntry(FeeType type, AssetCode asset, int64_t fixed, int64_t percent,
                                          AccountID *accountID, AccountType *accountType, int64_t subtype,
                                          int64_t lowerBound, int64_t upperBound, AssetCode *feeAsset) {
            FeeEntry fee;
            fee.feeType = type;
            fee.asset = asset;
            fee.fixedFee = fixed;
            fee.percentFee = percent;
            fee.subtype = subtype;

            if (accountID) {
                fee.accountID.activate() = *accountID;
            }
            if (accountType) {
                fee.accountType.activate() = *accountType;
            }

            fee.lowerBound = lowerBound;
            fee.upperBound = upperBound;

            fee.hash = FeeFrame::calcHash(type, asset, accountID, accountType, subtype);

            if (feeAsset != nullptr) {
                fee.ext.v(LedgerVersion::CROSS_ASSET_FEE);
                fee.ext.feeAsset() = *feeAsset;
            }

            return fee;
        }
    }
}