//
// Created by kfur on 5/18/18.
//

#include "IdentityPolicyChecker.h"
#include "ledger/AccountHelper.h"

#include <algorithm>
#include <cctype>
#include <functional>

namespace stellar
{

bool
IdentityPolicyChecker::doCheckPolicies(const AccountID &masterID,
                                       const PolicyDetails &policyDetails,
                                       Database &db,
                                       LedgerDelta* delta)
{
    bool isOwnerAllow = true;
    bool isAdminAllow = false;
    uint64_t lastPriority = 0;
    std::once_flag once;

    auto policies = IdentityPolicyChecker::loadPolicies(masterID, policyDetails, db, delta);

    std::sort(
        policies.begin(), policies.end(),
        [&masterID](IdentityPolicyFrame::pointer i, IdentityPolicyFrame::pointer j)
        {
            return i->getOwnerID() == masterID;
        }
        );

    for (const auto& p : policies)
    {
        if (p->getOwnerID() == masterID)
        {
            isAdminAllow = checkPolicy(p, lastPriority);
        }
        else if (isAdminAllow)
        {
            std::call_once(once, [&lastPriority]() { lastPriority = 0; });

            isOwnerAllow = checkPolicy(p, lastPriority);
        }
        else
        {
            break;
        }
    }

    return isAdminAllow && isOwnerAllow;
}

std::vector<IdentityPolicyFrame::pointer>
IdentityPolicyChecker::loadPolicies(const AccountID &masterID,
                                    const PolicyDetails &policyDetails,
                                    Database &db,
                                    LedgerDelta* delta)
{
    std::string sql = "SELECT "
                      "identity_policies.* "
                      "FROM identity_policies "
                      "INNER JOIN policy_attachment ON identity_policies.id = policy_attachment.policy_id "
                      "WHERE "
                      "(identity_policies.owner = :ow OR identity_policies.owner = :ms)                  AND "
                      "identity_policies.resource IN (:res)                                              AND "
                      "identity_policies.action = :ac                                                    AND "
                      "(policy_attachment.account_id = :aid OR policy_attachment.account_id IS NULL)     AND "
                      "(policy_attachment.account_type = :atp OR policy_attachment.account_type IS NULL)";

    auto prep = db.getPreparedStatement(sql);
    std::vector<stellar::IdentityPolicyFrame::pointer> result;

    const auto sourceAccount = AccountHelper::Instance()->loadAccount(policyDetails.mInitiator, db, delta);
    const auto accountType = static_cast<int32_t>(sourceAccount->getAccountType());

    uint64_t id;
    uint64_t priority;
    std::string resource;
    std::string action;
    int32_t effect;
    AccountID ownerIDStrKey;
    int32_t version;

    auto timer = db.getSelectTimer("identity_policy-join");

    LedgerEntry le;
    le.data.type(LedgerEntryType::IDENTITY_POLICY);

    std::string actIDStrKey;

    auto &st = prep.statement();
    st.exchange(soci::use(policyDetails.mPolicyOwner));
    st.exchange(soci::use(masterID));
    st.exchange(soci::use(policyDetails.mResourceIDs));
    st.exchange(soci::use(action));
    st.exchange(soci::use(policyDetails.mInitiator));
    st.exchange(soci::use(accountType));
    st.exchange(soci::into(id));
    st.exchange(soci::into(priority));
    st.exchange(soci::into(resource));
    st.exchange(soci::into(effect));
    st.exchange(soci::into(ownerIDStrKey));
    st.exchange(soci::into(version));
    st.exchange(soci::into(le.lastModifiedLedgerSeq));

    st.define_and_bind();
    st.execute(true);
    while (st.got_data())
    {
        auto policy = std::make_shared<IdentityPolicyFrame>(le);
        auto policyEntry = policy->getIdentityPolicy();

        policyEntry.id = id;
        policyEntry.priority = priority;
        policyEntry.action = action;
        policyEntry.resource = resource;
        policyEntry.effect = static_cast<Effect>(effect);
        policyEntry.ownerID = ownerIDStrKey;
        policyEntry.ext.v(static_cast<LedgerVersion>(version));

        IdentityPolicyFrame::ensureValid(policyEntry);

        result.push_back(policy);

        st.fetch();
    }

    return result;
}

bool
IdentityPolicyChecker::checkPolicy(IdentityPolicyFrame::pointer policy,
                                   uint64_t& lastPriority)
{
    bool result = false;

    if (lastPriority < policy->getPriority())
    {
        lastPriority = policy->getPriority();
        if (policy->getEffect() == Effect::ALLOW)
            result = true;
        else
            result = false;
    }
    else if (lastPriority == policy->getPriority() &&
             policy->getEffect() == Effect::DENY)
    {
        result = false;
    }

    return result;
}

} // namespace stellar