#include "SetAccountRoleOpFrame.h"
#include "ledger/AccountHelper.h"
#include "ledger/AccountRoleHelper.h"
#include "ledger/AccountRolePolicyHelper.h"
#include "ledger/LedgerDelta.h"
#include "ledger/LedgerHeaderFrame.h"

namespace stellar
{
using namespace std;

SetAccountRoleOpFrame::SetAccountRoleOpFrame(Operation const& op,
                                             OperationResult& res,
                                             TransactionFrame& parentTx)
    : OperationFrame(op, res, parentTx)
    , mSetAccountRole(mOperation.body.setAccountRoleOp())
{
}

std::unordered_map<AccountID, CounterpartyDetails>
SetAccountRoleOpFrame::getCounterpartyDetails(Database& db,
                                              LedgerDelta* delta) const
{
    // no counterparties
    return {};
}

SourceDetails
SetAccountRoleOpFrame::getSourceAccountDetails(
    std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
    int32_t ledgerVersion) const
{
    return SourceDetails(
        {AccountType::ANY}, mSourceAccount->getHighThreshold(),
        static_cast<int32_t>(SignerType::IDENTITY_POLICY_MANAGER));
}

bool
SetAccountRoleOpFrame::createAccountRole(Application& app,
                                         StorageHelper& storageHelper)
{
    LedgerKey ledgerKey;
    ledgerKey.type(LedgerEntryType::ACCOUNT_ROLE);
    ledgerKey.accountRole().accountRoleID = mSetAccountRole.id;

    if (storageHelper.getAccountRoleHelper().exists(ledgerKey))
    {
        innerResult().code(SetAccountRoleResultCode::MALFORMED);
        return false;
    }

    if (!storageHelper.getLedgerDelta())
    {
        throw std::runtime_error(
            "Unable to create account role without ledger");
    }
    LedgerDelta& delta = *storageHelper.getLedgerDelta();

    auto newAccountRoleID =
        delta.getHeaderFrame().generateID(LedgerEntryType::ACCOUNT_ROLE);
    auto frame = AccountRoleFrame::createNew(newAccountRoleID, getSourceID(),
                                             mSetAccountRole.data->name, delta);

    storageHelper.getAccountRoleHelper().storeAdd(frame->mEntry);

    innerResult().code(SetAccountRoleResultCode::SUCCESS);
    innerResult().success().accountRoleID = newAccountRoleID;
    return true;
}

bool
SetAccountRoleOpFrame::deleteAccountRole(Application& app,
                                         StorageHelper& storageHelper)
{
    LedgerKey ledgerKey;
    ledgerKey.type(LedgerEntryType::ACCOUNT_ROLE);
    ledgerKey.accountRole().accountRoleID = mSetAccountRole.id;

    auto accountRoleHelper = storageHelper.getAccountRoleHelper();
    auto result = accountRoleHelper.storeLoad(ledgerKey);

    if (result)
    {
        accountRoleHelper.storeDelete(ledgerKey);

        innerResult().code(SetAccountRoleResultCode::SUCCESS);
        innerResult().success().accountRoleID = mSetAccountRole.id;
        return true;
    }
    else
    {
        innerResult().code(SetAccountRoleResultCode::NOT_FOUND);
        return false;
    }
}

bool
SetAccountRoleOpFrame::doApply(Application& app, StorageHelper& storageHelper,
                               LedgerManager& ledgerManager)
{
    Database& db = ledgerManager.getDatabase();

    if (mSetAccountRole.data)
    {
        return createAccountRole(app, storageHelper);
    }
    else
    {
        return deleteAccountRole(app, storageHelper);
    }
}

bool
SetAccountRoleOpFrame::doCheckValid(Application& app)
{
    return true;
}
} // namespace stellar