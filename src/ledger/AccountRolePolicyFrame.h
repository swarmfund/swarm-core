#pragma once

#include "ledger/AccountFrame.h"
#include "ledger/EntryFrame.h"
#include "util/types.h"

#include <functional>
#include <stdio.h>
#include <unordered_map>

namespace stellar
{
class AccountRolePolicyFrame : public EntryFrame
{
    AccountRolePolicyEntry& mAccountRolePolicyEntry;
    AccountRolePolicyFrame(AccountRolePolicyFrame const& from);

  public:
    AccountRolePolicyFrame();
    explicit AccountRolePolicyFrame(LedgerEntry const& from);

    using pointer = std::shared_ptr<AccountRolePolicyFrame>;

    AccountRolePolicyEntry&
    getPolicyEntry() const
    {
        return mAccountRolePolicyEntry;
    }

    uint64_t
    getID() const
    {
        return mAccountRolePolicyEntry.accountRolePolicyID;
    }

    uint64_t
    getRoleID() const
    {
        return mAccountRolePolicyEntry.accountRoleID;
    }

    std::string
    getResource() const
    {
        return mAccountRolePolicyEntry.resource;
    }

    std::string
    getAction() const
    {
        return mAccountRolePolicyEntry.action;
    }

    AccountRolePolicyEffect
    getEffect() const
    {
        return mAccountRolePolicyEntry.effect;
    }

    EntryFrame::pointer copy() const override;

    static bool isEffectValid(AccountRolePolicyEffect const effect);

    static void ensureValid(AccountRolePolicyEntry const& entry);
    void ensureValid() const;
};

} // namespace stellar
