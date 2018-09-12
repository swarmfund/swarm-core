#include "SetAccountRolePolicyOpFrame.h"
#include "database/Database.h"
#include "ledger/AccountRolePolicyHelper.h"
#include "ledger/LedgerDelta.h"
#include "ledger/LedgerHeaderFrame.h"
#include "main/Application.h"
#include "medida/meter.h"
#include "medida/metrics_registry.h"

namespace stellar
{

using namespace std;
using xdr::operator==;

SetAccountRolePolicyOpFrame::SetAccountRolePolicyOpFrame(
    const Operation& op, OperationResult& res, TransactionFrame& parentTx)
    : OperationFrame(op, res, parentTx)
    , mSetAccountRolePolicy(mOperation.body.setAccountRolePolicyOp())
{
}

bool
SetAccountRolePolicyOpFrame::doApply(Application& app,
                                     StorageHelper& storageHelper,
                                     LedgerManager& ledgerManager)
{
    if (isDeleteOp(mSetAccountRolePolicy))
    {
        deleteAccountPolicy(app, storageHelper);
    }
    else
    {
        createOrUpdatePolicy(app, storageHelper);
    }
}

bool
SetAccountRolePolicyOpFrame::doCheckValid(Application& app)
{
    // if delete action then do not check other fields
    if (isDeleteOp(mSetAccountRolePolicy))
    {
        return true;
    }

    if (mSetAccountRolePolicy.data->resource.empty() ||
        mSetAccountRolePolicy.data->action.empty())
    {
        innerResult().code(SetAccountRolePolicyResultCode::MALFORMED);
        return false;
    }

    return true;
}

SourceDetails
SetAccountRolePolicyOpFrame::getSourceAccountDetails(
    std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
    int32_t ledgerVersion) const
{
    // allow for any user
    return SourceDetails(
        {AccountType::ANY}, mSourceAccount->getMediumThreshold(),
        static_cast<int32_t>(SignerType::IDENTITY_POLICY_MANAGER));
}

std::unordered_map<AccountID, CounterpartyDetails>
SetAccountRolePolicyOpFrame::getCounterpartyDetails(Database& db,
                                                    LedgerDelta* delta) const
{
    return {};
}

bool
SetAccountRolePolicyOpFrame::createOrUpdatePolicy(Application& app,
                                                  StorageHelper& storageHelper)
{
    LedgerHeaderFrame& headerFrame =
        storageHelper.getLedgerDelta().getHeaderFrame();

    LedgerEntry le;
    le.data.type(LedgerEntryType::ACCOUNT_ROLE_POLICY);
    le.data.accountRolePolicy().accountRolePolicyID =
        mSetAccountRolePolicy.id == 0
            ? headerFrame.generateID(LedgerEntryType::ACCOUNT_ROLE_POLICY)
            : mSetAccountRolePolicy.id;
    le.data.accountRolePolicy().accountRoleID =
        mSetAccountRolePolicy.data->roleID;
    le.data.accountRolePolicy().resource = mSetAccountRolePolicy.data->resource;
    le.data.accountRolePolicy().action = mSetAccountRolePolicy.data->action;
    le.data.accountRolePolicy().effect = mSetAccountRolePolicy.data->effect;
    le.data.accountRolePolicy().ownerID = getSourceID();

    auto key = LedgerEntryKey(le);
    if (!EntryHelperProvider::existsEntry(storageHelper.getDatabase(), key))
    {
        PolicyDetails details(mSetAccountRolePolicy.data->resource,
                              mSetAccountRolePolicy.data->action);
        if (IdentityPolicyChecker::findPolicy(
                mSetAccountRolePolicy.data->roleID, details,
                storageHelper.getDatabase(), &storageHelper.getLedgerDelta()) !=
            IdentityPolicyChecker::FindResult::NOT_FOUND)
        {
            innerResult().code(
                SetAccountRolePolicyResultCode::POLICY_ALREADY_EXISTS);
            return false;
        }
    }

    innerResult().code(SetAccountRolePolicyResultCode::SUCCESS);
    innerResult().success().accountRolePolicyID =
        le.data.accountRolePolicy().accountRolePolicyID;

    EntryHelperProvider::storeAddOrChangeEntry(storageHelper.getLedgerDelta(),
                                               storageHelper.getDatabase(), le);

    return true;
}
bool
SetAccountRolePolicyOpFrame::deleteAccountPolicy(Application& app,
                                                 StorageHelper& storageHelper)
{
    LedgerKey key;
    key.type(LedgerEntryType::ACCOUNT_ROLE_POLICY);
    key.accountRolePolicy().accountRolePolicyID = mSetAccountRolePolicy.id;
    key.accountRolePolicy().ownerID = getSourceID();

    auto frame = storageHelper.getAccountRolePolicyHelper().storeLoad(key);
    if (!frame)
    {
        innerResult().code(SetAccountRolePolicyResultCode::NOT_FOUND);
        return false;
    }

    EntryHelperProvider::storeDeleteEntry(storageHelper.getLedgerDelta(),
                                          storageHelper.getDatabase(),
                                          frame->getKey());
    innerResult().code(SetAccountRolePolicyResultCode::SUCCESS);
    innerResult().success().accountRolePolicyID = mSetAccountRolePolicy.id;
    return true;
}

bool
SetAccountRolePolicyOpFrame::isDeleteOp(
    const SetAccountRolePolicyOp& accountRolePolicyOp)
{
    return !accountRolePolicyOp.data;
}

} // namespace stellar