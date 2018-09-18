#include "IdentityPolicyChecker.h"
#include "ledger/AccountHelper.h"

#include <algorithm>
#include <cctype>
#include <functional>

namespace stellar
{

bool
IdentityPolicyChecker::isPolicyAllowed(const AccountID& initiatorID,
                                       const PolicyDetails& policyDetails,
                                       Database& db, LedgerDelta* delta)
{
    const auto sourceAccount =
        AccountHelper::Instance()->loadAccount(initiatorID, db, delta);
    if (!sourceAccount->getAccountRole())
    {
        // accounts with no role assigned will fail policy check
        return false;
    }
    return findPolicy(*sourceAccount->getAccountRole(), policyDetails, db) == FindResult::ALLOW;
}

IdentityPolicyChecker::FindResult
IdentityPolicyChecker::findPolicy(uint32 accountRole,
                                  const PolicyDetails& policyDetails,
                                  Database& db)
{
    const std::string sql = "SELECT effect "
                            "FROM account_role_policies "
                            "WHERE account_role_policies.role = :role "
                            "AND account_role_policies.resource = :res "
                            "AND account_role_policies.action = :ac";

    auto prep = db.getPreparedStatement(sql);
    std::vector<stellar::AccountRolePolicyFrame::pointer> result;

    int32 effect;
    AccountID ownerIDStrKey;

    auto timer = db.getSelectTimer("identity_policy-join");

    LedgerEntry le;
    le.data.type(LedgerEntryType::ACCOUNT_ROLE_POLICY);

    std::string actIDStrKey;

    auto& st = prep.statement();
    st.exchange(soci::use(accountRole));
    st.exchange(soci::use(policyDetails.getResourceID()));
    st.exchange(soci::use(policyDetails.getAction()));
    st.exchange(soci::into(effect));

    st.define_and_bind();
    st.execute(true);

    if (!st.got_data())
    {
        return FindResult::NOT_FOUND;
    }

    st.fetch();
    assert(!st.got_data());

    return effect == (int32)AccountRolePolicyEffect::ALLOW ? FindResult::ALLOW
                                                           : FindResult::DENY;
}

} // namespace stellar