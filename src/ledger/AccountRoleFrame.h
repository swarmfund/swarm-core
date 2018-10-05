#pragma once

#include "ledger/EntryFrame.h"

namespace soci
{
class session;
}

namespace stellar
{
class StatementContext;

class AccountRoleFrame : public EntryFrame
{
    AccountRoleEntry& mAccountRole;

    AccountRoleFrame(AccountRoleFrame const& from);

  public:
    typedef std::shared_ptr<AccountRoleFrame> pointer;

    AccountRoleFrame();
    explicit AccountRoleFrame(LedgerEntry const& from);

    AccountRoleFrame& operator=(AccountRoleFrame const& other);

    EntryFrame::pointer
    copy() const override
    {
        return EntryFrame::pointer(new AccountRoleFrame(*this));
    }

    AccountRoleEntry const&
    getAccountRole() const
    {
        return mAccountRole;
    }

    uint64_t
    getID() const
    {
        return mAccountRole.accountRoleID;
    }

    std::string
    getName() const
    {
        return mAccountRole.accountRoleName;
    }

    static void ensureValid(const LedgerEntry& entry);

    void ensureValid() const;

    static pointer createNew(uint64_t id,
                             std::string const& name,
                             LedgerDelta& delta);
};
} // namespace stellar