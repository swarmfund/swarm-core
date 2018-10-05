#include "ManageAccountRolePermissionOpFrame.h"
#include "database/Database.h"
#include "ledger/AccountRolePermissionHelperImpl.h"
#include "ledger/LedgerDelta.h"
#include "ledger/LedgerHeaderFrame.h"
#include "main/Application.h"
#include "medida/meter.h"
#include "medida/metrics_registry.h"

namespace stellar
{

using namespace std;
using xdr::operator==;

ManageAccountRolePermissionOpFrame::ManageAccountRolePermissionOpFrame(
    const Operation& op, OperationResult& res, TransactionFrame& parentTx)
    : OperationFrame(op, res, parentTx)
    , mManageAccountRolePermission(
          mOperation.body.manageAccountRolePermissionOp())
{
}

bool
ManageAccountRolePermissionOpFrame::doApply(Application& app,
                                            StorageHelper& storageHelper,
                                            LedgerManager& ledgerManager)
{
    switch (mManageAccountRolePermission.data.action())
    {
    case ManageAccountRolePermissionOpAction::CREATE:
    case ManageAccountRolePermissionOpAction::UPDATE:
        return createOrUpdatePolicy(app, storageHelper);
    case ManageAccountRolePermissionOpAction::REMOVE:
        return deleteAccountPolicy(app, storageHelper);
    default:
        throw std::runtime_error("Unknown action.");
    }
}

bool
ManageAccountRolePermissionOpFrame::doCheckValid(Application& app)
{
    return isValidEnumValue(mManageAccountRolePermission.data.action());
}

SourceDetails
ManageAccountRolePermissionOpFrame::getSourceAccountDetails(
    std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
    int32_t ledgerVersion) const
{
    // allow for any user
    return SourceDetails(
        getAllAccountTypes(), mSourceAccount->getMediumThreshold(),
        static_cast<int32_t>(SignerType::ACCOUNT_ROLE_PERMISSION_MANAGER));
}

std::unordered_map<AccountID, CounterpartyDetails>
ManageAccountRolePermissionOpFrame::getCounterpartyDetails(
    Database& db, LedgerDelta* delta) const
{
    return {};
}

bool
ManageAccountRolePermissionOpFrame::createOrUpdatePolicy(
    Application& app, StorageHelper& storageHelper)
{
    if (!storageHelper.getLedgerDelta())
    {
        throw std::runtime_error("Unable to process policy without ledger.");
    }
    LedgerHeaderFrame& headerFrame =
        storageHelper.getLedgerDelta()->getHeaderFrame();
    auto& helper = storageHelper.getAccountRolePermissionHelper();

    LedgerEntry le;
    le.data.type(LedgerEntryType::ACCOUNT_ROLE_PERMISSION);

    auto& lePermission = le.data.accountRolePermission();
    switch (mManageAccountRolePermission.data.action())
    {
    case ManageAccountRolePermissionOpAction::CREATE:
        lePermission.permissionID =
            headerFrame.generateID(LedgerEntryType::ACCOUNT_ROLE_PERMISSION);
        lePermission.accountRoleID =
            mManageAccountRolePermission.data.createData().roleID;
        lePermission.opType =
            mManageAccountRolePermission.data.createData().opType;
        break;
    case ManageAccountRolePermissionOpAction::UPDATE:
        lePermission.permissionID =
            mManageAccountRolePermission.data.updateData().permissionID;
        lePermission.accountRoleID =
            mManageAccountRolePermission.data.updateData().roleID;
        lePermission.opType =
            mManageAccountRolePermission.data.updateData().opType;
        break;
    default:
        throw std::runtime_error("Unexpected action type.");
    }

    innerResult().code(ManageAccountRolePermissionResultCode::SUCCESS);
    innerResult().success().permissionID =
        le.data.accountRolePermission().permissionID;

    if (helper.exists(helper.getLedgerKey(le)))
    {
        helper.storeChange(le);
    }
    else
    {
        helper.storeAdd(le);
    }

    return true;
}

bool
ManageAccountRolePermissionOpFrame::deleteAccountPolicy(
    Application& app, StorageHelper& storageHelper)
{
    LedgerKey key;
    key.type(LedgerEntryType::ACCOUNT_ROLE_PERMISSION);
    key.accountRolePermission().permissionID =
        mManageAccountRolePermission.data.removeData().permissionID;

    auto frame = storageHelper.getAccountRolePermissionHelper().storeLoad(key);
    if (!frame)
    {
        innerResult().code(ManageAccountRolePermissionResultCode::NOT_FOUND);
        return false;
    }

    storageHelper.getAccountRolePermissionHelper().storeDelete(frame->getKey());
    innerResult().code(ManageAccountRolePermissionResultCode::SUCCESS);
    innerResult().success().permissionID =
        key.accountRolePermission().permissionID;
    return true;
}

} // namespace stellar