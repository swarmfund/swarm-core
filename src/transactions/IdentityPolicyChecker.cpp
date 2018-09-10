#include "IdentityPolicyChecker.h"
#include "ledger/AccountHelper.h"

#include <algorithm>
#include <cctype>
#include <functional>

namespace stellar
{

bool
IdentityPolicyChecker::doCheckPolicies(const AccountID& initiatorID,
                                       const PolicyDetails& policyDetails,
                                       Database& db, LedgerDelta* delta)
{
    const auto sourceAccount = AccountHelper::Instance()->loadAccount(
            initiatorID, db, delta);
    const auto accountType =
            static_cast<int32_t>(sourceAccount->getAccountRole());
    const std::string sql = "SELECT id, resource, effect, "
                            "ownerid, version, lastmodified "
                            "FROM identity_policies "
                            "WHERE identity_policies.role = :role "
                            "AND identity_policies.resource = :res "
                            "AND identity_policies.action = :ac";

    auto prep = db.getPreparedStatement(sql);
    std::vector<stellar::AccountRolePolicyFrame::pointer> result;

    uint64_t id;
    std::string resource;
    std::string action;
    int32_t effect;
    AccountID ownerIDStrKey;
    int32_t version;

    auto timer = db.getSelectTimer("identity_policy-join");

    LedgerEntry le;
    le.data.type(LedgerEntryType::IDENTITY_POLICY);

    std::string actIDStrKey;

    auto& st = prep.statement();
    st.exchange(soci::use(accountType));
    st.exchange(soci::use(policyDetails.getResourceID()));
    st.exchange(soci::use(policyDetails.getAction()));
    st.exchange(soci::into(id));
    st.exchange(soci::into(resource));
    st.exchange(soci::into(effect));
    st.exchange(soci::into(ownerIDStrKey));
    st.exchange(soci::into(version));
    st.exchange(soci::into(le.lastModifiedLedgerSeq));

    st.define_and_bind();
    st.execute(true);

    if (!st.got_data())
    {
        return false;
    }

    auto policy = std::make_shared<AccountRolePolicyFrame>(le);
    auto policyEntry = policy->getIdentityPolicy();

    policyEntry.id = id;
    policyEntry.action = action;
    policyEntry.resource = resource;
    policyEntry.effect = static_cast<Effect>(effect);
    policyEntry.ownerID = ownerIDStrKey;
    policyEntry.ext.v(static_cast<LedgerVersion>(version));

    AccountRolePolicyFrame::ensureValid(policyEntry);

    result.push_back(policy);

    st.fetch();
    assert(!st.got_data());

    return policyEntry.effect == Effect::ALLOW;
}

bool
IdentityPolicyChecker::checkPolicy(AccountRolePolicyFrame::pointer policy,
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