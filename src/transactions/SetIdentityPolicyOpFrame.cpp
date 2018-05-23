
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

const uint64_t SetIdentityPolicyOpFrame::policiesAmountLimit = 100;

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

    return trySetIdentityPolicy(db, delta);
}

bool
SetIdentityPolicyOpFrame::doCheckValid(Application& app)
{
    const auto countOfOwnerPolicies =
        IdentityPolicyHelper::Instance()->countObjectsForOwner(mSourceAccount->getID(),
                                                               app.getDatabase().getSession());
    if (countOfOwnerPolicies >= policiesAmountLimit)
    {
        innerResult().code(SetIdentityPolicyResultCode::POLICIES_LIMIT_EXCEED);
        return false;
    }

    if (mSetIdentityPolicy.resource.empty() || mSetIdentityPolicy.action.empty())
    {
        innerResult().code(SetIdentityPolicyResultCode::MALFORMED);
        return false;
    }

    if (
        mSetIdentityPolicy.priority < PRIORITY_USER_MIN      ||
        (mSetIdentityPolicy.priority > PRIORITY_USER_MAX &&
         mSetIdentityPolicy.priority < PRIORITY_ADMIN_MIN)   ||
        mSetIdentityPolicy.priority > PRIORITY_ADMIN_MAX
        )
    {
        innerResult().code(SetIdentityPolicyResultCode::INVALID_PRIORITY);
        return false;
    }

    return true;
}

bool
SetIdentityPolicyOpFrame::trySetIdentityPolicy(Database& db, LedgerDelta& delta)
{
    auto identityPolicyHelper = IdentityPolicyHelper::Instance();
    auto identityPolicyFrame = identityPolicyHelper->loadIdentityPolicy(
        mSetIdentityPolicy.id, mSourceAccount->getID(), db, &delta);


    // delete
    if (mSetIdentityPolicy.isDelete)
    {
        if (!identityPolicyFrame)
        {
            innerResult().code(SetIdentityPolicyResultCode::NOT_FOUND);
            return false;
        }

        EntryHelperProvider::storeDeleteEntry(delta, db,
                                              identityPolicyFrame->getKey());
        return true;
    }

    // update
    if (identityPolicyFrame)
    {
        auto& entityType = identityPolicyFrame->getIdentityPolicy();
        entityType.id = mSetIdentityPolicy.id;
        EntryHelperProvider::storeChangeEntry(delta, db,
                                              identityPolicyFrame->mEntry);
        return true;
    }

    // create
    LedgerEntry le;
    le.data.type(LedgerEntryType::IDENTITY_POLICY);
    le.data.identityPolicy().id = mSetIdentityPolicy.id;
    le.data.identityPolicy().priority = mSetIdentityPolicy.priority;
    le.data.identityPolicy().resource = mSetIdentityPolicy.resource;
    le.data.identityPolicy().effect = mSetIdentityPolicy.effect;
    le.data.identityPolicy().ownerID = getSourceAccount().getID();

    identityPolicyFrame = make_shared<IdentityPolicyFrame>(le);
    EntryHelperProvider::storeAddEntry(delta, db, identityPolicyFrame->mEntry);

    return true;
}

SourceDetails
SetIdentityPolicyOpFrame::getSourceAccountDetails(
    std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
    int32_t ledgerVersion) const
{

    if (mSetIdentityPolicy.priority >= PRIORITY_USER_MIN &&
        mSetIdentityPolicy.priority <= PRIORITY_USER_MAX)
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
    else if (mSetIdentityPolicy.priority >= PRIORITY_ADMIN_MIN &&
             mSetIdentityPolicy.priority <= PRIORITY_ADMIN_MAX)
    {
        return SourceDetails(
            {AccountType::MASTER}, mSourceAccount->getMediumThreshold(),
            static_cast<int32_t>(SignerType::IDENTITY_POLICY_MANAGER));
    }
    // allow for no one
    return SourceDetails(
        {}, mSourceAccount->getMediumThreshold(),
        static_cast<int32_t>(SignerType::IDENTITY_POLICY_MANAGER));
}

std::unordered_map<AccountID, CounterpartyDetails>
SetIdentityPolicyOpFrame::getCounterpartyDetails(Database& db,
                                                 LedgerDelta* delta) const
{
    return {};
}

} // namespace stellar