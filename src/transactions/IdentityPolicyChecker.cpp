#include "IdentityPolicyChecker.h"
#include "ledger/AccountHelper.h"
#include "xdr/Stellar-types.h"

namespace stellar
{

bool
IdentityPolicyChecker::isPolicyAllowed(
    const AccountFrame::pointer initiatorAccountFrame,
    const OperationType opType, Database& db)
{
    if (!initiatorAccountFrame->getAccountRole())
    {
        // accounts with no role assigned will fail policy check
        return false;
    }
    return checkPolicy(*initiatorAccountFrame->getAccountRole(), opType, db);
}

bool
IdentityPolicyChecker::checkPolicy(uint32 accountRole,
                                   const OperationType opType, Database& db)
{
    const std::string sql = "SELECT EXISTS (SELECT NULL "
                            "FROM account_role_permissions "
                            "WHERE role = :role AND operation_type = :o)";

    auto prep = db.getPreparedStatement(sql);
    std::vector<stellar::AccountRolePermissionFrame::pointer> result;

    int32 exists;
    AccountID ownerIDStrKey;

    auto timer = db.getSelectTimer("identity_policy-join");

    LedgerEntry le;
    le.data.type(LedgerEntryType::ACCOUNT_ROLE_PERMISSION);

    std::string actIDStrKey;

    auto& st = prep.statement();
    st.exchange(soci::use(accountRole));
    st.exchange(soci::use(static_cast<int32>(opType)));
    st.exchange(soci::into(exists));

    st.define_and_bind();
    st.execute(true);

    return exists != 0;
}

} // namespace stellar