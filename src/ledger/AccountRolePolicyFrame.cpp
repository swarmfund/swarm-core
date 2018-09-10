
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
    : EntryFrame(LedgerEntryType::IDENTITY_POLICY)
    , mIdentityPolicyEntry(mEntry.data.identityPolicy())
{
}

AccountRolePolicyFrame::AccountRolePolicyFrame(LedgerEntry const& from)
    : EntryFrame(from), mIdentityPolicyEntry(mEntry.data.identityPolicy())
{
}

AccountRolePolicyFrame::AccountRolePolicyFrame(AccountRolePolicyFrame const& from)
    : AccountRolePolicyFrame(from.mEntry)
{
}

void
AccountRolePolicyFrame::ensureValid(const IdentityPolicyEntry& mIdentityPolicyEntry)
{
    if (!AccountRolePolicyFrame::isEffectValid(mIdentityPolicyEntry.effect))
    {
        throw std::runtime_error("Identity policy effect invalid");
    }
}

void
AccountRolePolicyFrame::ensureValid() const
{
    ensureValid(mIdentityPolicyEntry);
}

bool AccountRolePolicyFrame::isEffectValid(Effect const effect)
{
    return isValidEnumValue(effect);
}

EntryFrame::pointer
AccountRolePolicyFrame::copy() const
{
    return EntryFrame::pointer(new AccountRolePolicyFrame(*this));
}

} // namespace stellar
