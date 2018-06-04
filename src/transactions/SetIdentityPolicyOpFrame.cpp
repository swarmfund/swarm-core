
#include "SetIdentityPolicyOpFrame.h"
#include "database/Database.h"
#include "ledger/IdentityPolicyHelper.h"
#include "ledger/LedgerDelta.h"
#include "main/Application.h"
#include "medida/meter.h"
#include "medida/metrics_registry.h"

#include <algorithm>
#include <regex>
#include <tuple>

namespace stellar
{

using namespace std;
using xdr::operator==;

SetIdentityPolicyOpFrame::SetIdentityPolicyOpFrame(const Operation& op,
                                                   OperationResult& res,
                                                   TransactionFrame& parentTx)
    : OperationFrame(op, res, parentTx)
    , mSetIdentityPolicy(mOperation.body.setIdentityPolicyOp())
{
}

bool
SetIdentityPolicyOpFrame::doApply(Application& app, LedgerDelta& delta,
                                  LedgerManager& ledgerManager)
{
    Database& db = ledgerManager.getDatabase();
    innerResult().code(SetIdentityPolicyResultCode::SUCCESS);

    auto const sourceID = getSourceID();

    if (!(sourceID == app.getMasterID()))
    {
        const auto countOfOwnerPolicies =
                IdentityPolicyHelper::Instance()->countObjectsForOwner(sourceID,
                                                                       app.getDatabase().getSession());
        if (countOfOwnerPolicies >= app.getMaxIdentityPoliciesPerAccount())
        {
            innerResult().code(SetIdentityPolicyResultCode::POLICIES_LIMIT_EXCEED);
            return false;
        }
    }

    return trySetIdentityPolicy(db, delta);
}

bool
SetIdentityPolicyOpFrame::doCheckValid(Application& app)
{
    // if delete action then do not check other fields
    if (mSetIdentityPolicy.data.get() == nullptr)
    {
        return true;
    }

    if (mSetIdentityPolicy.data->resource.empty() || mSetIdentityPolicy.data->action.empty())
    {
        innerResult().code(SetIdentityPolicyResultCode::MALFORMED);
        return false;
    }

    // Priority checks
    const auto priority = mSetIdentityPolicy.data->priority;

    if (priority >= PRIORITY_USER_MIN &&
        priority <= PRIORITY_USER_MAX &&
        getSourceID() == app.getMasterID())
    {
        innerResult().code(SetIdentityPolicyResultCode::MALFORMED);
        return false;
    }// High priority allowed only for master account
    else if (priority >= PRIORITY_ADMIN_MIN &&
             priority <= PRIORITY_ADMIN_MAX &&
            !(getSourceID() == app.getMasterID()))
    {
        innerResult().code(SetIdentityPolicyResultCode::MALFORMED);
        return false;
    }

    // Invalide priority (goes beyond borders)
    if (priority < PRIORITY_USER_MIN      ||
        (priority > PRIORITY_USER_MAX   &&
         priority < PRIORITY_ADMIN_MIN)   ||
        priority > PRIORITY_ADMIN_MAX)
    {
        innerResult().code(SetIdentityPolicyResultCode::MALFORMED);
        return false;
    }

    return true;
}

bool
SetIdentityPolicyOpFrame::trySetIdentityPolicy(Database& db, LedgerDelta& delta)
{
    // delete
    if (mSetIdentityPolicy.data.get() == nullptr)
    {
        auto identityPolicyFrame = IdentityPolicyHelper::Instance()->loadIdentityPolicy(
                mSetIdentityPolicy.id, getSourceID(), db, &delta);

        if (!identityPolicyFrame)
        {
            innerResult().code(SetIdentityPolicyResultCode::NOT_FOUND);
            return false;
        }

        EntryHelperProvider::storeDeleteEntry(delta, db,
                                              identityPolicyFrame->getKey());
        return true;
    }

    // create or update
    LedgerEntry le;
    le.data.type(LedgerEntryType::IDENTITY_POLICY);
    le.data.identityPolicy().id = mSetIdentityPolicy.id == 0
                                  ? delta.getHeaderFrame().generateID(LedgerEntryType::IDENTITY_POLICY)
                                  : mSetIdentityPolicy.id;
    le.data.identityPolicy().priority = mSetIdentityPolicy.data->priority;
    le.data.identityPolicy().resource = mSetIdentityPolicy.data->resource;
    le.data.identityPolicy().action = mSetIdentityPolicy.data->action;
    le.data.identityPolicy().effect = mSetIdentityPolicy.data->effect;
    le.data.identityPolicy().ownerID = getSourceID();

    EntryHelperProvider::storeAddOrChangeEntry(delta, db, le);

    return true;
}

SourceDetails
SetIdentityPolicyOpFrame::getSourceAccountDetails(
    std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
    int32_t ledgerVersion) const
{
    // allow for any user
    return SourceDetails(
        {AccountType::MASTER, AccountType::COMMISSION,
         AccountType::EXCHANGE, AccountType::GENERAL,
         AccountType::OPERATIONAL, AccountType::SYNDICATE,
         AccountType::NOT_VERIFIED},
        mSourceAccount->getMediumThreshold(),
        static_cast<int32_t>(SignerType::IDENTITY_POLICY_MANAGER));
}

std::unordered_map<AccountID, CounterpartyDetails>
SetIdentityPolicyOpFrame::getCounterpartyDetails(Database& db,
                                                 LedgerDelta* delta) const
{
    return {};
}

} // namespace stellar