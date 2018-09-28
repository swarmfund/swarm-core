#include "ManageAccountRolePolicyOpFrame.h"
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

ManageAccountRolePolicyOpFrame::ManageAccountRolePolicyOpFrame(
    const Operation& op, OperationResult& res, TransactionFrame& parentTx)
    : OperationFrame(op, res, parentTx)
    , mManageAccountRolePolicy(mOperation.body.manageAccountRolePolicyOp())
{
}

bool
ManageAccountRolePolicyOpFrame::doApply(Application& app,
                                        StorageHelper& storageHelper,
                                        LedgerManager& ledgerManager)
{
    switch (mManageAccountRolePolicy.data.action())
    {
    case ManageAccountRolePolicyOpAction::CREATE:
    case ManageAccountRolePolicyOpAction::UPDATE:
        return createOrUpdatePolicy(app, storageHelper);
    case ManageAccountRolePolicyOpAction::REMOVE:
        return deleteAccountPolicy(app, storageHelper);
    default:
        assert(false);
    }
}

bool
ManageAccountRolePolicyOpFrame::doCheckValid(Application& app)
{
    switch (mManageAccountRolePolicy.data.action())
    {
    case ManageAccountRolePolicyOpAction::CREATE:
        if (mManageAccountRolePolicy.data.createData().resource.empty())
        {
            innerResult().code(
                ManageAccountRolePolicyResultCode::EMPTY_RESOURCE);
            return false;
        }
        if (mManageAccountRolePolicy.data.createData().action.empty())
        {
            innerResult().code(ManageAccountRolePolicyResultCode::EMPTY_ACTION);
            return false;
        }
        break;
    case ManageAccountRolePolicyOpAction::UPDATE:
        if (mManageAccountRolePolicy.data.updateData().resource.empty())
        {
            innerResult().code(
                ManageAccountRolePolicyResultCode::EMPTY_RESOURCE);
            return false;
        }
        if (mManageAccountRolePolicy.data.updateData().action.empty())
        {
            innerResult().code(ManageAccountRolePolicyResultCode::EMPTY_ACTION);
            return false;
        }
        break;
    case ManageAccountRolePolicyOpAction::REMOVE:
        return true;
    default:
        assert(false);
    }
    return true;
}

SourceDetails
ManageAccountRolePolicyOpFrame::getSourceAccountDetails(
    std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
    int32_t ledgerVersion) const
{
    // allow for any user
    return SourceDetails(
        getAllAccountTypes(), mSourceAccount->getMediumThreshold(),
        static_cast<int32_t>(SignerType::ACCOUNT_ROLE_POLICY_MANAGER));
}

std::unordered_map<AccountID, CounterpartyDetails>
ManageAccountRolePolicyOpFrame::getCounterpartyDetails(Database& db,
                                                       LedgerDelta* delta) const
{
    return {};
}

bool
ManageAccountRolePolicyOpFrame::createOrUpdatePolicy(
    Application& app, StorageHelper& storageHelper)
{
    if (!storageHelper.getLedgerDelta())
    {
        throw std::runtime_error("Unable to process policy without ledger.");
    }
    LedgerHeaderFrame& headerFrame =
        storageHelper.getLedgerDelta()->getHeaderFrame();
    auto& helper = storageHelper.getAccountRolePolicyHelper();

    LedgerEntry le;
    le.data.type(LedgerEntryType::ACCOUNT_ROLE_POLICY);

    auto& lePolicy = le.data.accountRolePolicy();
    lePolicy.ownerID = getSourceID();
    switch (mManageAccountRolePolicy.data.action())
    {
    case ManageAccountRolePolicyOpAction::CREATE:
        lePolicy.accountRolePolicyID =
            headerFrame.generateID(LedgerEntryType::ACCOUNT_ROLE_POLICY);
        lePolicy.accountRoleID =
            mManageAccountRolePolicy.data.createData().roleID;
        lePolicy.resource = mManageAccountRolePolicy.data.createData().resource;
        lePolicy.action = mManageAccountRolePolicy.data.createData().action;
        lePolicy.effect = mManageAccountRolePolicy.data.createData().effect;
        break;
    case ManageAccountRolePolicyOpAction::UPDATE:
        lePolicy.accountRolePolicyID =
            mManageAccountRolePolicy.data.updateData().policyID;
        lePolicy.accountRoleID =
            mManageAccountRolePolicy.data.updateData().roleID;
        lePolicy.resource = mManageAccountRolePolicy.data.updateData().resource;
        lePolicy.action = mManageAccountRolePolicy.data.updateData().action;
        lePolicy.effect = mManageAccountRolePolicy.data.updateData().effect;
        break;
    default:
        assert(false);
    }

    bool shouldCheckExistingPolicies = true;
    if (mManageAccountRolePolicy.data.action() ==
        ManageAccountRolePolicyOpAction::UPDATE)
    {
        LedgerKey key = helper.getLedgerKey(le);
        EntryFrame::pointer existingFrame = helper.storeLoad(key);
        AccountRolePolicyFrame::pointer existingPolicyFrame =
            std::dynamic_pointer_cast<AccountRolePolicyFrame>(existingFrame);
        assert(existingPolicyFrame);
        shouldCheckExistingPolicies =
            !(existingPolicyFrame->getResource() == lePolicy.resource &&
              existingPolicyFrame->getAction() == lePolicy.action &&
              existingPolicyFrame->getRoleID() == lePolicy.accountRoleID);
    }
    if (shouldCheckExistingPolicies)
    {
        PolicyDetails details{lePolicy.resource, lePolicy.action};
        if (IdentityPolicyChecker::findPolicy(lePolicy.accountRoleID, details,
                                              storageHelper.getDatabase()) !=
            IdentityPolicyChecker::FindResult::NOT_FOUND)
        {
            innerResult().code(
                ManageAccountRolePolicyResultCode::POLICY_ALREADY_EXISTS);
            return false;
        }
    }

    innerResult().code(ManageAccountRolePolicyResultCode::SUCCESS);
    innerResult().success().accountRolePolicyID =
        le.data.accountRolePolicy().accountRolePolicyID;

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
ManageAccountRolePolicyOpFrame::deleteAccountPolicy(
    Application& app, StorageHelper& storageHelper)
{
    LedgerKey key;
    key.type(LedgerEntryType::ACCOUNT_ROLE_POLICY);
    key.accountRolePolicy().accountRolePolicyID =
        mManageAccountRolePolicy.data.removeData().policyID;
    key.accountRolePolicy().ownerID = getSourceID();

    auto frame = storageHelper.getAccountRolePolicyHelper().storeLoad(key);
    if (!frame)
    {
        innerResult().code(ManageAccountRolePolicyResultCode::NOT_FOUND);
        return false;
    }

    storageHelper.getAccountRolePolicyHelper().storeDelete(frame->getKey());
    innerResult().code(ManageAccountRolePolicyResultCode::SUCCESS);
    innerResult().success().accountRolePolicyID =
        key.accountRolePolicy().accountRolePolicyID;
    return true;
}

} // namespace stellar