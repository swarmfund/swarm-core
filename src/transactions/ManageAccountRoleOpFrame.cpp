#include "ManageAccountRoleOpFrame.h"
#include "ledger/AccountHelper.h"
#include "ledger/AccountRoleHelper.h"
#include "ledger/AccountRolePermissionHelperImpl.h"
#include "ledger/LedgerDelta.h"
#include "ledger/LedgerHeaderFrame.h"
#include <xdr/Stellar-operation-manage-account-role.h>

namespace stellar
{
using namespace std;

ManageAccountRoleOpFrame::ManageAccountRoleOpFrame(Operation const& op,
                                                   OperationResult& res,
                                                   TransactionFrame& parentTx)
    : OperationFrame(op, res, parentTx)
    , mManageAccountRole(mOperation.body.manageAccountRoleOp())
{
}

std::unordered_map<AccountID, CounterpartyDetails>
ManageAccountRoleOpFrame::getCounterpartyDetails(Database& db,
                                                 LedgerDelta* delta) const
{
    // no counterparties
    return {};
}

SourceDetails
ManageAccountRoleOpFrame::getSourceAccountDetails(
    std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
    int32_t ledgerVersion) const
{
    return SourceDetails(
        getAllAccountTypes(), mSourceAccount->getHighThreshold(),
        static_cast<int32_t>(SignerType::ACCOUNT_ROLE_PERMISSION_MANAGER));
}

bool
ManageAccountRoleOpFrame::createAccountRole(Application& app,
                                            StorageHelper& storageHelper)
{
    auto& data = mManageAccountRole.data.createData();

    if (!storageHelper.getLedgerDelta())
    {
        throw std::runtime_error(
            "Unable to create account role without ledger");
    }
    LedgerDelta& delta = *storageHelper.getLedgerDelta();

    auto newAccountRoleID =
        delta.getHeaderFrame().generateID(LedgerEntryType::ACCOUNT_ROLE);
    auto frame = AccountRoleFrame::createNew(newAccountRoleID,
                                             data.name, delta);

    storageHelper.getAccountRoleHelper().storeAdd(frame->mEntry);

    innerResult().code(ManageAccountRoleResultCode::SUCCESS);
    innerResult().success().accountRoleID = newAccountRoleID;
    return true;
}

bool
ManageAccountRoleOpFrame::deleteAccountRole(Application& app,
                                            StorageHelper& storageHelper)
{
    auto& data = mManageAccountRole.data.removeData();

    LedgerKey ledgerKey;
    ledgerKey.type(LedgerEntryType::ACCOUNT_ROLE);
    ledgerKey.accountRole().accountRoleID = data.accountRoleID;

    auto& accountRoleHelper = storageHelper.getAccountRoleHelper();
    auto result = accountRoleHelper.storeLoad(ledgerKey);

    if (result)
    {
        accountRoleHelper.storeDelete(ledgerKey);

        innerResult().code(ManageAccountRoleResultCode::SUCCESS);
        innerResult().success().accountRoleID = data.accountRoleID;
        return true;
    }
    else
    {
        innerResult().code(ManageAccountRoleResultCode::NOT_FOUND);
        return false;
    }
}

bool
ManageAccountRoleOpFrame::doApply(Application& app,
                                  StorageHelper& storageHelper,
                                  LedgerManager& ledgerManager)
{
    Database& db = ledgerManager.getDatabase();

    switch (mManageAccountRole.data.action())
    {
    case ManageAccountRoleOpAction::CREATE:
        return createAccountRole(app, storageHelper);
    case ManageAccountRoleOpAction::REMOVE:
        return deleteAccountRole(app, storageHelper);
    default:
        throw std::runtime_error("Unknown action.");
    }
}

bool
ManageAccountRoleOpFrame::doCheckValid(Application& app)
{
    return true;
}
} // namespace stellar