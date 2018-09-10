#include "AccountRoleFrame.h"
#include "AccountFrame.h"

using namespace soci;
using namespace std;

namespace stellar
{
using xdr::operator<;

AccountRoleFrame::AccountRoleFrame()
    : EntryFrame(LedgerEntryType::ACCOUNT_ROLE)
    , mAccountRole(mEntry.data.accountRole())
{
}

AccountRoleFrame::AccountRoleFrame(LedgerEntry const& from)
    : EntryFrame(from), mAccountRole(mEntry.data.accountRole())
{
}

AccountRoleFrame::AccountRoleFrame(AccountRoleFrame const& from)
    : AccountRoleFrame(from.mEntry)
{
}

AccountRoleFrame&
AccountRoleFrame::operator=(const AccountRoleFrame& other)
{
    if (&other != this)
    {
        mAccountRole = other.mAccountRole;
        mKey = other.mKey;
        mKeyCalculated = other.mKeyCalculated;
    }
    return *this;
}

void
AccountRoleFrame::ensureValid(const AccountRoleEntry& entry)
{
    // TODO
}

void
AccountRoleFrame::ensureValid() const
{
    return ensureValid(mAccountRole);
}

AccountRoleFrame::pointer
AccountRoleFrame::createNew(uint64_t id, AccountID const& ownerID,
                            std::string const& name, LedgerDelta& delta)
{
    LedgerEntry entry;
    entry.data.type(LedgerEntryType::ACCOUNT_ROLE);
    auto& accountRole = entry.data.accountRole();
    accountRole.accountRoleID = id;
    accountRole.ownerID = ownerID;
    accountRole.accountRoleName = name;

    return make_shared<AccountRoleFrame>(entry);
}
} // namespace stellar