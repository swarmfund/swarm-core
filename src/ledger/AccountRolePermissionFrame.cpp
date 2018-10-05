#include "AccountRolePermissionFrame.h"
#include "LedgerDelta.h"
#include "database/Database.h"
#include "lib/util/format.h"
#include "util/basen.h"
#include "util/types.h"

#include <exception>

namespace stellar
{

using xdr::operator<;

AccountRolePermissionFrame::AccountRolePermissionFrame()
    : EntryFrame(LedgerEntryType::ACCOUNT_ROLE_PERMISSION)
    , mAccountRolePermissionEntry(mEntry.data.accountRolePermission())
{
}

AccountRolePermissionFrame::AccountRolePermissionFrame(LedgerEntry const& from)
    : EntryFrame(from)
    , mAccountRolePermissionEntry(mEntry.data.accountRolePermission())
{
}

AccountRolePermissionFrame::AccountRolePermissionFrame(
    AccountRolePermissionFrame const& from)
    : AccountRolePermissionFrame(from.mEntry)
{
}

void
AccountRolePermissionFrame::ensureValid(const AccountRolePermissionEntry& entry)
{
    if (!isValidEnumValue(entry.opType))
    {
        throw std::runtime_error("Invalid operation type");
    }
}

void
AccountRolePermissionFrame::ensureValid() const
{
    ensureValid(mAccountRolePermissionEntry);
}

EntryFrame::pointer
AccountRolePermissionFrame::copy() const
{
    return EntryFrame::pointer(new AccountRolePermissionFrame(*this));
}

} // namespace stellar
