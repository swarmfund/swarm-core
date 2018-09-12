#include "AccountRolePolicyFrame.h"
#include "LedgerDelta.h"
#include "database/Database.h"
#include "lib/util/format.h"
#include "util/basen.h"
#include "util/types.h"

#include <exception>

namespace stellar
{

using xdr::operator<;

AccountRolePolicyFrame::AccountRolePolicyFrame()
    : EntryFrame(LedgerEntryType::ACCOUNT_ROLE_POLICY)
    , mAccountRolePolicyEntry(mEntry.data.accountRolePolicy())
{
}

AccountRolePolicyFrame::AccountRolePolicyFrame(LedgerEntry const& from)
    : EntryFrame(from), mAccountRolePolicyEntry(mEntry.data.accountRolePolicy())
{
}

AccountRolePolicyFrame::AccountRolePolicyFrame(
    AccountRolePolicyFrame const& from)
    : AccountRolePolicyFrame(from.mEntry)
{
}

void
AccountRolePolicyFrame::ensureValid(const AccountRolePolicyEntry& entry)
{
    if (!AccountRolePolicyFrame::isEffectValid(entry.effect))
    {
        throw std::runtime_error("Identity policy effect invalid");
    }
}

void
AccountRolePolicyFrame::ensureValid() const
{
    ensureValid(mAccountRolePolicyEntry);
}

bool
AccountRolePolicyFrame::isEffectValid(AccountRolePolicyEffect effect)
{
    return isValidEnumValue(effect);
}

EntryFrame::pointer
AccountRolePolicyFrame::copy() const
{
    return EntryFrame::pointer(new AccountRolePolicyFrame(*this));
}

} // namespace stellar
