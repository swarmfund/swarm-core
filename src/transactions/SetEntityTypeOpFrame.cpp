
#include "SetEntityTypeOpFrame.h"
#include "database/Database.h"
#include "ledger/EntityTypeHelper.h"
#include "ledger/LedgerDelta.h"
#include "main/Application.h"
#include "medida/meter.h"
#include "medida/metrics_registry.h"

namespace stellar
{

using namespace std;
using xdr::operator==;

SetEntityTypeOpFrame::SetEntityTypeOpFrame(const Operation& op,
                                           OperationResult& res,
                                           TransactionFrame& parentTx)
    : OperationFrame(op, res, parentTx)
    , mSetEntityType(mOperation.body.setEntityTypeOp())
{
}

bool
SetEntityTypeOpFrame::doApply(Application& app, LedgerDelta& delta,
                              LedgerManager& ledgerManager)
{
    Database& db = ledgerManager.getDatabase();
    innerResult().code(SetEntityTypeResultCode::SUCCESS);

    return trySetEntityType(db, delta);
}

bool
SetEntityTypeOpFrame::doCheckValid(Application& app)
{
    if (!EntityTypeFrame::isNameValid(mSetEntityType.entityType.name))
    {
        innerResult().code(SetEntityTypeResultCode::INVALID_NAME);
        return false;
    }
    if (!EntityTypeFrame::isTypeValid(mSetEntityType.entityType.type))
    {
        innerResult().code(SetEntityTypeResultCode::INVALID_TYPE);
        return false;
    }
    if(mSetEntityType.entityType.id == 0){
        innerResult().code(SetEntityTypeResultCode::MALFORMED);
        return false;
    }

    return true;
}

bool
SetEntityTypeOpFrame::trySetEntityType(Database& db, LedgerDelta& delta)
{
    auto entityTypeHelper = EntityTypeHelper::Instance();
    auto entityTypeFrame = entityTypeHelper->loadEntityType(
        mSetEntityType.entityType.id, mSetEntityType.entityType.type, db,
        &delta);

    // delete
    if (mSetEntityType.isDelete)
    {
        if (!entityTypeFrame)
        {
            innerResult().code(SetEntityTypeResultCode::NOT_FOUND);
            return false;
        }
        entityTypeHelper->storeDelete(delta, db, entityTypeFrame->getKey());

        return true;
    }

    // update
    if (entityTypeFrame)
    {
        auto& entityType = entityTypeFrame->getEntityType();
        entityType.id = mSetEntityType.entityType.id;
        entityType.type = mSetEntityType.entityType.type;
        entityType.name = mSetEntityType.entityType.name;
        entityTypeHelper->storeChange(delta, db, entityTypeFrame->mEntry);

        return true;
    }

    // create
    LedgerEntry le;
    le.data.type(LedgerEntryType::ENTITY_TYPE);
    le.data.entityType() = mSetEntityType.entityType;
    entityTypeFrame = make_shared<EntityTypeFrame>(le);
    entityTypeHelper->storeAdd(delta, db, entityTypeFrame->mEntry);

    return true;
}

SourceDetails
SetEntityTypeOpFrame::getSourceAccountDetails(
    std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
    int32_t ledgerVersion) const
{
    return SourceDetails(
        {
            AccountType::MASTER,
        },
        mSourceAccount->getMediumThreshold(),
        static_cast<int32_t>(SignerType::ENTITY_TYPE_MANAGER));
}
std::unordered_map<AccountID, CounterpartyDetails>
SetEntityTypeOpFrame::getCounterpartyDetails(Database& db,
                                             LedgerDelta* delta) const
{
    return {};
}

} // namespace stellar